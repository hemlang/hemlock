/*
 * Hemlock regex flag translation (shared by interpreter and runtime).
 *
 * @stdlib/regex exports its own flag values (REG_EXTENDED = 1, REG_ICASE = 2,
 * REG_NOSUB = 4, REG_NEWLINE = 8, REG_NOTBOL = 1, REG_NOTEOL = 2). They do
 * not match every libc's <regex.h> (glibc has REG_NEWLINE = 4 and
 * REG_NOSUB = 8), so passing them to regcomp()/regexec() unchanged turns
 * REG_NEWLINE into REG_NOSUB, after which regexec() never fills pmatch.
 * Always map through these helpers. Include after <regex.h>.
 */
#ifndef HEMLOCK_REGEX_FLAGS_H
#define HEMLOCK_REGEX_FLAGS_H

#define HML_REG_EXTENDED  1
#define HML_REG_ICASE     2
#define HML_REG_NOSUB     4
#define HML_REG_NEWLINE   8

#define HML_REG_NOTBOL    1
#define HML_REG_NOTEOL    2

static inline int hml_regex_native_cflags(long long flags) {
    int native = 0;
    if (flags & HML_REG_EXTENDED) native |= REG_EXTENDED;
    if (flags & HML_REG_ICASE)    native |= REG_ICASE;
    if (flags & HML_REG_NOSUB)    native |= REG_NOSUB;
    if (flags & HML_REG_NEWLINE)  native |= REG_NEWLINE;
    return native;
}

static inline int hml_regex_native_eflags(long long flags) {
    int native = 0;
    if (flags & HML_REG_NOTBOL) native |= REG_NOTBOL;
    if (flags & HML_REG_NOTEOL) native |= REG_NOTEOL;
    return native;
}

// regexec() wrapper for callers that read match offsets. A regex compiled
// with REG_NOSUB matches without filling pmatch, which would leave the
// offsets uninitialized; pre-set them to -1 and report such a match as
// REG_NOMATCH so callers never index text with garbage offsets.
static inline int hml_regexec_positions(const regex_t *preg, const char *text,
                                        size_t nmatch, regmatch_t *pmatch, int eflags) {
    for (size_t i = 0; i < nmatch; i++) {
        pmatch[i].rm_so = -1;
        pmatch[i].rm_eo = -1;
    }
    int rc = regexec(preg, text, nmatch, pmatch, eflags);
    if (rc == 0 && nmatch > 0 && pmatch[0].rm_so < 0) return REG_NOMATCH;
    return rc;
}

// Iterate over successive non-overlapping matches of `preg` in text[0..len).
// *pos is the scan position (start at 0). On a match, stores the byte
// offsets of the whole match in *so/*eo, advances *pos past it (by one
// UTF-8 character for an empty match), and returns 1; returns 0 when done.
static inline int hml_regex_next_match(const regex_t *preg, const char *text, size_t len,
                                       size_t *pos, size_t *so, size_t *eo) {
    if (*pos > len) return 0;
    regmatch_t m;
    if (hml_regexec_positions(preg, text + *pos, 1, &m, *pos > 0 ? REG_NOTBOL : 0) != 0) {
        return 0;
    }
    *so = *pos + (size_t)m.rm_so;
    *eo = *pos + (size_t)m.rm_eo;
    if (*eo == *so) {
        size_t adv = 1;
        if (*eo < len) {
            unsigned char c = (unsigned char)text[*eo];
            adv = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        }
        *pos = *eo + adv;
    } else {
        *pos = *eo;
    }
    return 1;
}

#endif // HEMLOCK_REGEX_FLAGS_H
