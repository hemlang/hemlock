#!/bin/bash
#
# Portable `timeout` wrapper for Hemlock's test scripts.
#
# Source this file from a bash test script:
#
#   source "$(dirname "${BASH_SOURCE[0]}")/lib/timeout.sh"
#
# It defines a shell function `timeout` with GNU timeout's calling convention:
#
#   timeout [-s SIGNAL] [-k DURATION] DURATION COMMAND [ARG...]
#
# Resolution order:
#   1. the system `timeout` binary (GNU coreutils, standard on Linux)
#   2. `gtimeout` (macOS with `brew install coreutils`)
#   3. a Perl fallback (perl ships with macOS) using fork/exec + alarm
#
# Exit codes match GNU timeout:
#   124            command timed out (and exited after the first signal)
#   137            command had to be killed with SIGKILL (128 + 9)
#   125            timeout itself failed (bad arguments, fork failure)
#   126            command found but not executable
#   127            command not found
#   128+N          command was terminated by signal N (not by us)
#   otherwise      the command's own exit status
#
# DURATION is a number with an optional suffix: s (default), m, h, d.
# A duration of 0 disables the timeout.

# Only define once, even if sourced multiple times.
if [ -n "${HML_TIMEOUT_SH_LOADED:-}" ]; then
    return 0 2>/dev/null || exit 0
fi
HML_TIMEOUT_SH_LOADED=1

# Resolve an executable on PATH, ignoring shell functions/aliases/builtins
# (e.g. our own `timeout` function). Works in bash, zsh and sh.
_hml_find_exe() {
    local p
    p="$(command -v "$1" 2>/dev/null)" || return 1
    case "$p" in
        /*) [ -x "$p" ] && printf '%s\n' "$p" ;;
        *)  return 1 ;;
    esac
}

# Pick the implementation once, at source time.
if HML_TIMEOUT_BIN="$(_hml_find_exe timeout)"; then
    HML_TIMEOUT_IMPL="system"
elif HML_TIMEOUT_BIN="$(_hml_find_exe gtimeout)"; then
    HML_TIMEOUT_IMPL="gtimeout"
elif _hml_find_exe perl >/dev/null; then
    HML_TIMEOUT_BIN=""
    HML_TIMEOUT_IMPL="perl"
else
    HML_TIMEOUT_BIN=""
    HML_TIMEOUT_IMPL="none"
fi

# Perl implementation of GNU timeout semantics.
# Arguments: SIGNAL KILL_AFTER DURATION COMMAND [ARG...]
# shellcheck disable=SC2016  # $vars below are Perl, not shell
HML_TIMEOUT_PERL='
use strict;
use warnings;
use POSIX ();

my ($sig_name, $kill_after, $duration, @cmd) = @ARGV;

sub to_seconds {
    my ($s) = @_;
    return undef unless defined $s && $s =~ /^(\d+(?:\.\d*)?|\.\d+)([smhd]?)$/;
    my %mult = ("" => 1, s => 1, m => 60, h => 3600, d => 86400);
    return $1 * $mult{$2};
}

my $secs = to_seconds($duration);
my $kill_secs = to_seconds($kill_after);
if (!defined $secs || !defined $kill_secs) {
    print STDERR "timeout: invalid time interval\n";
    exit 125;
}
if (!@cmd) {
    print STDERR "timeout: missing command\n";
    exit 125;
}

$sig_name =~ s/^SIG//i;
$sig_name = uc $sig_name;
if ($sig_name !~ /^\d+$/ && !exists $SIG{$sig_name}) {
    print STDERR "timeout: invalid signal: $sig_name\n";
    exit 125;
}

# alarm() takes whole seconds; round sub-second durations up so they still fire.
sub whole { my ($s) = @_; my $i = int($s); return ($s > $i) ? $i + 1 : $i; }

my $pid = fork();
if (!defined $pid) {
    print STDERR "timeout: fork failed: $!\n";
    exit 125;
}

if ($pid == 0) {
    # Child: own process group so we can signal the whole tree.
    setpgrp(0, 0);
    $SIG{ALRM} = "DEFAULT";
    { no warnings "exec"; exec { $cmd[0] } @cmd; }
    my $err = $!;
    my $not_found = $!{ENOENT};
    print STDERR "timeout: failed to run command \x27$cmd[0]\x27: $err\n";
    POSIX::_exit($not_found ? 127 : 126);
}

# Parent. Make sure the child has its process group before we may signal it.
eval { setpgrp($pid, $pid) };

my $timed_out = 0;
my $killed = 0;

sub signal_child {
    my ($sig) = @_;
    kill $sig, -$pid;   # process group
    kill $sig, $pid;    # and the child directly, in case setpgrp raced
}

# Forward termination signals to the child, like GNU timeout does.
for my $s (qw(INT TERM HUP QUIT)) {
    $SIG{$s} = sub { signal_child($s); };
}

$SIG{ALRM} = sub {
    if (!$timed_out) {
        $timed_out = 1;
        signal_child($sig_name);
        # GNU timeout also sends SIGCONT so a stopped child can handle the signal.
        signal_child("CONT") if $sig_name ne "KILL" && $sig_name ne "9";
        alarm(whole($kill_secs)) if $kill_secs > 0;
    } else {
        $killed = 1;
        signal_child("KILL");
    }
};

alarm(whole($secs)) if $secs > 0;

my $status;
while (1) {
    my $r = waitpid($pid, 0);
    if ($r == $pid) { $status = $?; last; }
    next if $r == -1 && $!{EINTR};
    # Should not happen, but do not spin.
    print STDERR "timeout: waitpid failed: $!\n";
    exit 125;
}
alarm(0);

my $exit_sig = $status & 127;
my $exit_code = $status >> 8;

if ($killed || ($timed_out && $exit_sig == 9)) {
    exit 137;
}
if ($timed_out) {
    exit 124;
}
if ($exit_sig) {
    exit 128 + $exit_sig;
}
exit $exit_code;
'

timeout() {
    case "$HML_TIMEOUT_IMPL" in
        system|gtimeout)
            "$HML_TIMEOUT_BIN" "$@"
            return
            ;;
        none)
            echo "timeout: no timeout implementation found (install coreutils or perl)" >&2
            return 125
            ;;
    esac

    # Perl fallback: parse the GNU options our scripts may use.
    local sig="TERM"
    local kill_after="0"
    while [ $# -gt 0 ]; do
        case "$1" in
            -s|--signal)      sig="$2"; shift 2 ;;
            --signal=*)       sig="${1#*=}"; shift ;;
            -s*)              sig="${1#-s}"; shift ;;
            -k|--kill-after)  kill_after="$2"; shift 2 ;;
            --kill-after=*)   kill_after="${1#*=}"; shift ;;
            -k*)              kill_after="${1#-k}"; shift ;;
            --preserve-status|--foreground|-v|--verbose)
                # Accepted for compatibility; not implemented by the fallback.
                shift ;;
            --) shift; break ;;
            -*)
                echo "timeout: unsupported option: $1" >&2
                return 125
                ;;
            *) break ;;
        esac
    done

    if [ $# -lt 2 ]; then
        echo "timeout: usage: timeout [-s SIGNAL] [-k DURATION] DURATION COMMAND [ARG...]" >&2
        return 125
    fi

    perl -e "$HML_TIMEOUT_PERL" -- "$sig" "$kill_after" "$@"
}
