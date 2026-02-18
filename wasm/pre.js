// Hemlock WASM pre-init
// This file is prepended to the Emscripten output.
// It sets up the Module object for browser/Node.js environments.

if (typeof Module === 'undefined') {
    Module = {};
}
