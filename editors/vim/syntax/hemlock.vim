" Vim syntax file
" Language: Hemlock
" Maintainer: Hemlock Contributors
" Latest Revision: 2025-11-30

if exists("b:current_syntax")
  finish
endif

" Keywords
syn keyword hemlockKeyword let fn if else while for in return break continue switch case default defer
syn keyword hemlockKeyword import from export define enum const
syn keyword hemlockAsync async await spawn join detach
syn keyword hemlockException try catch finally throw panic
syn keyword hemlockSelf self args contained

" Types
syn keyword hemlockType i8 i16 i32 i64 u8 u16 u32 u64 f32 f64
syn keyword hemlockType bool string rune ptr buffer array object void null
syn keyword hemlockType integer number byte file task channel

" Constants
syn keyword hemlockBoolean true false
syn keyword hemlockNull null

" Signal constants (@stdlib/signal)
syn keyword hemlockSignal SIGINT SIGTERM SIGQUIT SIGHUP SIGABRT
syn keyword hemlockSignal SIGUSR1 SIGUSR2 SIGALRM SIGCHLD SIGCONT
syn keyword hemlockSignal SIGSTOP SIGTSTP SIGPIPE SIGTTIN SIGTTOU

" Socket constants (@stdlib/net)
syn keyword hemlockSocketConst AF_INET AF_INET6 SOCK_STREAM SOCK_DGRAM
syn keyword hemlockSocketConst IPPROTO_TCP IPPROTO_UDP SOL_SOCKET
syn keyword hemlockSocketConst SO_REUSEADDR SO_KEEPALIVE SO_RCVTIMEO SO_SNDTIMEO
syn keyword hemlockSocketConst POLLIN POLLOUT POLLERR POLLHUP POLLNVAL POLLPRI

" Math constants
syn keyword hemlockMathConst PI E TAU INF NAN

" Regex constants
syn keyword hemlockRegexConst REG_ICASE

" Global built-in functions (no import needed)
syn keyword hemlockBuiltin print write eprint read_line typeof alloc free memset memcpy
syn keyword hemlockBuiltin realloc talloc sizeof buffer channel open exec exec_argv
syn keyword hemlockBuiltin panic assert apply

" Stdlib functions (require import from @stdlib modules)
syn keyword hemlockStdlibFn signal raise
syn keyword hemlockStdlibFn sin cos tan asin acos atan atan2 sqrt pow exp
syn keyword hemlockStdlibFn log log10 log2 floor ceil round trunc
syn keyword hemlockStdlibFn floori ceili roundi trunci div divi
syn keyword hemlockStdlibFn abs min max clamp rand rand_range seed
syn keyword hemlockStdlibFn now sleep time_ms clock localtime gmtime mktime strftime
syn keyword hemlockStdlibFn getenv setenv unsetenv get_pid
syn keyword hemlockStdlibFn socket_create dns_resolve poll

" Pointer/FFI/Atomic builtins (global, no import needed)
syn keyword hemlockBuiltin ptr_offset ptr_null ptr_to_buffer buffer_ptr ffi_sizeof
syn keyword hemlockBuiltin atomic_fence

" Operators
syn match hemlockOperator "\v\+|-|\*|\/|%"
syn match hemlockOperator "\v\&|\||~|\^|<<|>>"
syn match hemlockOperator "\v\=\=|!\=|<\=|>\=|<|>"
syn match hemlockOperator "\v\&\&|\|\||!"
syn match hemlockOperator "\v\="

" Numbers
syn match hemlockNumber "\v<\d+>"
syn match hemlockFloat "\v<\d+\.\d+([eE][+-]?\d+)?>"
syn match hemlockHex "\v<0[xX][0-9a-fA-F]+>"
syn match hemlockBinary "\v<0[bB][01]+>"
syn match hemlockOctal "\v<0[oO][0-7]+>"

" Strings
syn region hemlockString start='"' end='"' contains=hemlockEscape,hemlockUnicodeEscape
syn match hemlockEscape contained "\\[ntr\\\"'0]"
syn match hemlockUnicodeEscape contained "\\u{[0-9a-fA-F]\{1,6\}}"

" Runes (character literals)
syn region hemlockRune start="'" end="'" contains=hemlockEscape,hemlockUnicodeEscape

" Template strings (backtick strings with ${} interpolation)
syn region hemlockTemplateString start='`' end='`' contains=hemlockTemplateInterpolation,hemlockEscape,hemlockUnicodeEscape,hemlockTemplateEscape
syn region hemlockTemplateInterpolation matchgroup=hemlockInterpolationDelim start='\${' end='}' contained contains=TOP
syn match hemlockTemplateEscape contained "\\[\$`]"

" Comments
syn keyword hemlockTodo TODO FIXME XXX NOTE contained
syn match hemlockComment "//.*$" contains=hemlockTodo
syn region hemlockBlockComment start="/\*" end="\*/" contains=hemlockTodo

" Function definitions
syn match hemlockFunction "\v<\w+>\s*\ze\("

" Delimiters
syn match hemlockDelimiter "[{}()\[\];,.]"

" Highlighting
hi def link hemlockKeyword Keyword
hi def link hemlockAsync Keyword
hi def link hemlockException Exception
hi def link hemlockSelf Special
hi def link hemlockType Type
hi def link hemlockBoolean Boolean
hi def link hemlockNull Constant
hi def link hemlockSignal Constant
hi def link hemlockSocketConst Constant
hi def link hemlockMathConst Constant
hi def link hemlockRegexConst Constant
hi def link hemlockBuiltin Function
hi def link hemlockStdlibFn Function
hi def link hemlockOperator Operator
hi def link hemlockNumber Number
hi def link hemlockFloat Float
hi def link hemlockHex Number
hi def link hemlockBinary Number
hi def link hemlockOctal Number
hi def link hemlockString String
hi def link hemlockRune Character
hi def link hemlockTemplateString String
hi def link hemlockTemplateEscape SpecialChar
hi def link hemlockInterpolationDelim Special
hi def link hemlockEscape SpecialChar
hi def link hemlockUnicodeEscape SpecialChar
hi def link hemlockComment Comment
hi def link hemlockBlockComment Comment
hi def link hemlockTodo Todo
hi def link hemlockFunction Function
hi def link hemlockDelimiter Delimiter

let b:current_syntax = "hemlock"
