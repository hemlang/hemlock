" Vim syntax file
" Language: Hemlock
" Maintainer: Hemlock Contributors
" Latest Revision: 2025-01-22

if exists("b:current_syntax")
  finish
endif

" Keywords
syn keyword hemlockKeyword let fn if else while for return break continue switch case default defer
syn keyword hemlockKeyword import from define
syn keyword hemlockAsync async await spawn join detach
syn keyword hemlockException try catch finally throw panic
syn keyword hemlockSelf self args contained

" Types
syn keyword hemlockType i8 i16 i32 i64 u8 u16 u32 u64 f32 f64
syn keyword hemlockType bool string rune ptr buffer array object void null
syn keyword hemlockType integer number byte

" Constants
syn keyword hemlockBoolean true false
syn keyword hemlockNull null

" Signal constants
syn keyword hemlockSignal SIGINT SIGTERM SIGQUIT SIGHUP SIGABRT
syn keyword hemlockSignal SIGUSR1 SIGUSR2 SIGALRM SIGCHLD SIGCONT
syn keyword hemlockSignal SIGSTOP SIGTSTP SIGPIPE SIGTTIN SIGTTOU

" Math constants
syn keyword hemlockMathConst PI E TAU INF NAN

" Regex constants
syn keyword hemlockRegexConst REG_ICASE

" Built-in functions
syn keyword hemlockBuiltin print typeof alloc free memset memcpy realloc
syn keyword hemlockBuiltin buffer channel signal raise open exec assert

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
hi def link hemlockMathConst Constant
hi def link hemlockRegexConst Constant
hi def link hemlockBuiltin Function
hi def link hemlockOperator Operator
hi def link hemlockNumber Number
hi def link hemlockFloat Float
hi def link hemlockHex Number
hi def link hemlockBinary Number
hi def link hemlockOctal Number
hi def link hemlockString String
hi def link hemlockRune Character
hi def link hemlockEscape SpecialChar
hi def link hemlockUnicodeEscape SpecialChar
hi def link hemlockComment Comment
hi def link hemlockBlockComment Comment
hi def link hemlockTodo Todo
hi def link hemlockFunction Function
hi def link hemlockDelimiter Delimiter

let b:current_syntax = "hemlock"
