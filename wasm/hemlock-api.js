/**
 * Hemlock WASM JavaScript API
 *
 * A clean JavaScript wrapper around the Hemlock WebAssembly interpreter.
 * Works in both browser and Node.js environments.
 *
 * Usage (browser):
 *   <script src="hemlock.js"></script>
 *   <script src="hemlock-api.js"></script>
 *   <script>
 *     Hemlock.init().then(function(hemlock) {
 *       hemlock.eval('print("hello");');
 *     });
 *   </script>
 *
 * Usage (Node.js):
 *   const { Hemlock } = require('./hemlock-api.js');
 *   Hemlock.init('./hemlock.js').then(function(hemlock) {
 *     hemlock.eval('print("hello");');
 *   });
 *
 * Three API layers:
 *   1. hemlock.eval(source)       - Stateless one-shot execution
 *   2. hemlock.createContext()     - Persistent context (variables survive across evals)
 *   3. hemlock.compile(source)    - Cached script (parse once, run many)
 */

(function(root) {
    'use strict';

    /* ====================================================================
     * HemlockContext - Persistent interpreter context
     * ==================================================================== */

    /**
     * A persistent Hemlock interpreter context.
     * Variables and functions defined in one eval() call are visible in later calls.
     *
     * Do not construct directly - use hemlock.createContext().
     *
     * @param {number} handle - Internal context handle
     * @param {object} fns - Wrapped C functions
     */
    function HemlockContext(handle, fns) {
        this._handle = handle;
        this._fns = fns;
        this._destroyed = false;
    }

    /**
     * Execute Hemlock source code in this context.
     * Variables defined here persist for future eval() calls.
     *
     * @param {string} source - Hemlock source code
     * @returns {{ ok: boolean, error: string|null }}
     *   ok=true on success, ok=false with error message on failure
     */
    HemlockContext.prototype.eval = function(source) {
        if (this._destroyed) {
            return { ok: false, error: 'Context has been destroyed' };
        }
        var rc = this._fns.ctxEval(this._handle, source);
        if (rc === 0) {
            return { ok: true, error: null };
        }
        var msg = this._fns.ctxLastError(this._handle);
        if (rc === 1) {
            return { ok: false, error: msg || 'Parse error' };
        }
        if (rc === 2) {
            return { ok: false, error: msg || 'Runtime error' };
        }
        return { ok: false, error: msg || 'Invalid context handle' };
    };

    /**
     * Read a variable from this context, returned as a parsed JavaScript value.
     *
     * @param {string} name - Variable name
     * @returns {*} The variable's value (parsed from JSON), or undefined if not found
     */
    HemlockContext.prototype.get = function(name) {
        if (this._destroyed) { return undefined; }
        var json = this._fns.ctxGet(this._handle, name);
        if (json === null || json === undefined || json === '') {
            return undefined;
        }
        try {
            return JSON.parse(json);
        } catch (e) {
            // Return raw string if not valid JSON (e.g. function values)
            return json;
        }
    };

    /**
     * Read a variable from this context as a raw JSON string.
     *
     * @param {string} name - Variable name
     * @returns {string|null} JSON string, or null if not found
     */
    HemlockContext.prototype.getJSON = function(name) {
        if (this._destroyed) { return null; }
        var json = this._fns.ctxGet(this._handle, name);
        if (json === null || json === undefined || json === '') {
            return null;
        }
        return json;
    };

    /**
     * Inject a value into this context. The value is serialized to JSON.
     *
     * @param {string} name - Variable name
     * @param {*} value - Value to inject (must be JSON-serializable)
     * @returns {boolean} true on success, false on error
     */
    HemlockContext.prototype.set = function(name, value) {
        if (this._destroyed) { return false; }
        var json = JSON.stringify(value);
        if (json === undefined) { return false; }
        var rc = this._fns.ctxSet(this._handle, name, json);
        return rc === 0;
    };

    /**
     * Inject a raw JSON string into this context.
     *
     * @param {string} name - Variable name
     * @param {string} json - JSON string representing the value
     * @returns {boolean} true on success, false on error
     */
    HemlockContext.prototype.setJSON = function(name, json) {
        if (this._destroyed) { return false; }
        var rc = this._fns.ctxSet(this._handle, name, json);
        return rc === 0;
    };

    /**
     * Get the error message from the last failed eval() or run().
     *
     * @returns {string|null} Error message, or null if the last call succeeded
     */
    HemlockContext.prototype.lastError = function() {
        if (this._destroyed) { return null; }
        var err = this._fns.ctxLastError(this._handle);
        return (err === null || err === undefined || err === '') ? null : err;
    };

    /**
     * Destroy this context and free all associated memory.
     * After calling destroy(), all other methods become no-ops.
     */
    HemlockContext.prototype.destroy = function() {
        if (this._destroyed) { return; }
        this._fns.ctxDestroy(this._handle);
        this._destroyed = true;
        this._handle = 0;
    };

    /* ====================================================================
     * HemlockScript - Cached (pre-compiled) script
     * ==================================================================== */

    /**
     * A pre-compiled Hemlock script. Parse once, execute many times.
     * Ideal for hot event handlers, animation callbacks, or per-frame logic.
     *
     * Do not construct directly - use hemlock.compile().
     *
     * @param {number} handle - Internal script handle
     * @param {object} fns - Wrapped C functions
     */
    function HemlockScript(handle, fns) {
        this._handle = handle;
        this._fns = fns;
        this._freed = false;
    }

    /**
     * Execute this cached script in a persistent context.
     * The script's AST is reused without re-parsing.
     *
     * @param {HemlockContext} context - The context to execute in
     * @returns {{ ok: boolean, error: string|null }}
     */
    HemlockScript.prototype.run = function(context) {
        if (this._freed) {
            return { ok: false, error: 'Script has been freed' };
        }
        if (context._destroyed) {
            return { ok: false, error: 'Context has been destroyed' };
        }
        var rc = this._fns.runScript(context._handle, this._handle);
        if (rc === 0) {
            return { ok: true, error: null };
        }
        var msg = context.lastError();
        return { ok: false, error: msg || 'Script execution error (code ' + rc + ')' };
    };

    /**
     * Free this cached script and release its AST.
     *
     * WARNING: Ensure no context still holds function values that reference
     * this script's AST. Destroy or reset relevant contexts first.
     */
    HemlockScript.prototype.free = function() {
        if (this._freed) { return; }
        this._fns.freeScript(this._handle);
        this._freed = true;
        this._handle = 0;
    };

    /* ====================================================================
     * Hemlock - Main API entry point
     * ==================================================================== */

    /**
     * Main Hemlock WASM API.
     * Use Hemlock.init() to create an instance.
     *
     * @param {object} module - Emscripten Module object
     * @param {object} fns - Wrapped C functions
     */
    function Hemlock(module, fns) {
        this._module = module;
        this._fns = fns;

        /** @type {string} Hemlock version string */
        this.version = fns.version();
    }

    /**
     * Initialize the Hemlock WASM module and return a ready-to-use API instance.
     *
     * @param {string|object} [options] - Path to hemlock.js (Node.js), or options object
     * @param {string} [options.wasmUrl] - URL/path to hemlock.js
     * @param {function} [options.print] - stdout handler (default: console.log)
     * @param {function} [options.printErr] - stderr handler (default: console.error)
     * @param {object} [options.Module] - Pre-configured Emscripten Module (browser)
     * @returns {Promise<Hemlock>} Initialized Hemlock API instance
     *
     * @example
     * // Browser (hemlock.js already loaded via <script>)
     * var hemlock = await Hemlock.init();
     *
     * @example
     * // Browser with custom output
     * var hemlock = await Hemlock.init({
     *     print: function(text) { myOutput.textContent += text + '\n'; },
     *     printErr: function(text) { myErrors.textContent += text + '\n'; }
     * });
     *
     * @example
     * // Node.js
     * var hemlock = await Hemlock.init('./wasm/hemlock.js');
     *
     * @example
     * // Node.js with options
     * var hemlock = await Hemlock.init({
     *     wasmUrl: './wasm/hemlock.js',
     *     print: function(text) { process.stdout.write(text + '\n'); }
     * });
     */
    Hemlock.init = function(options) {
        var opts = {};
        if (typeof options === 'string') {
            opts.wasmUrl = options;
        } else if (options && typeof options === 'object') {
            opts = options;
        }

        return new Promise(function(resolve, reject) {
            var Module;

            // Check if Module is already provided (pre-configured)
            if (opts.Module) {
                Module = opts.Module;
            }
            // Check for global Module (browser, already loaded via <script>)
            else if (typeof root !== 'undefined' && root.Module &&
                     typeof root.Module.cwrap === 'function') {
                Module = root.Module;
            }
            // Node.js: require the Emscripten JS file
            else if (opts.wasmUrl && typeof require === 'function') {
                try {
                    var path = require('path');
                    var resolved = path.resolve(opts.wasmUrl);
                    Module = require(resolved);
                } catch (e) {
                    reject(new Error('Failed to load hemlock.js: ' + e.message));
                    return;
                }
            }
            // Browser: Module not loaded yet
            else if (typeof root !== 'undefined' && root.Module) {
                Module = root.Module;
            }
            else {
                reject(new Error(
                    'Hemlock WASM module not found. ' +
                    'In browser: load hemlock.js via <script> before calling init(). ' +
                    'In Node.js: pass the path to hemlock.js as an argument.'
                ));
                return;
            }

            // Apply I/O overrides if provided
            if (opts.print) { Module.print = opts.print; }
            if (opts.printErr) { Module.printErr = opts.printErr; }

            function bootstrap() {
                try {
                    var fns = wrapFunctions(Module);
                    var instance = new Hemlock(Module, fns);
                    resolve(instance);
                } catch (e) {
                    reject(new Error('Failed to initialize Hemlock API: ' + e.message));
                }
            }

            // If cwrap is already available, the module is initialized
            if (typeof Module.cwrap === 'function') {
                // Still give Emscripten a tick to finish if needed
                if (typeof Module.calledRun !== 'undefined' || Module._hemlock_version) {
                    bootstrap();
                } else {
                    // Hook into onRuntimeInitialized
                    var original = Module.onRuntimeInitialized;
                    Module.onRuntimeInitialized = function() {
                        if (original) { original.call(Module); }
                        bootstrap();
                    };
                }
            }
            // Module returned a promise (newer Emscripten)
            else if (typeof Module.then === 'function') {
                Module.then(function(mod) {
                    Module = mod;
                    bootstrap();
                }).catch(reject);
            }
            // Wait for onRuntimeInitialized
            else {
                var original = Module.onRuntimeInitialized;
                Module.onRuntimeInitialized = function() {
                    if (original) { original.call(Module); }
                    bootstrap();
                };
            }
        });
    };

    /**
     * Wrap all exported C functions using Module.cwrap().
     * @private
     */
    function wrapFunctions(Module) {
        return {
            eval:        Module.cwrap('hemlock_eval',              'number',  ['string']),
            version:     Module.cwrap('hemlock_version',           'string',  []),
            ctxCreate:   Module.cwrap('hemlock_context_create',    'number',  []),
            ctxEval:     Module.cwrap('hemlock_context_eval',      'number',  ['number', 'string']),
            ctxDestroy:  Module.cwrap('hemlock_context_destroy',   null,      ['number']),
            ctxGet:      Module.cwrap('hemlock_context_get',       'string',  ['number', 'string']),
            ctxSet:      Module.cwrap('hemlock_context_set',       'number',  ['number', 'string', 'string']),
            ctxLastError:Module.cwrap('hemlock_context_last_error','string',  ['number']),
            compile:     Module.cwrap('hemlock_compile_script',    'number',  ['string']),
            runScript:   Module.cwrap('hemlock_run_script',        'number',  ['number', 'number']),
            freeScript:  Module.cwrap('hemlock_free_script',       null,      ['number'])
        };
    }

    /**
     * Execute Hemlock source code (stateless, one-shot).
     * Each call is independent - variables do not persist.
     * Output goes to the print/printErr handlers configured at init().
     *
     * @param {string} source - Hemlock source code to execute
     * @returns {number} 0 on success
     */
    Hemlock.prototype.eval = function(source) {
        return this._fns.eval(source);
    };

    /**
     * Create a new persistent context.
     * Variables and functions defined in eval() calls persist across calls.
     *
     * @returns {HemlockContext} A new context, or throws on failure
     *
     * @example
     * var ctx = hemlock.createContext();
     * ctx.eval('let score = 0;');
     * ctx.eval('score = score + 10;');
     * ctx.get('score');  // 10
     * ctx.destroy();
     */
    Hemlock.prototype.createContext = function() {
        var handle = this._fns.ctxCreate();
        if (handle === 0) {
            throw new Error(
                'Failed to create context (max ' +
                'concurrent contexts may be exceeded)'
            );
        }
        return new HemlockContext(handle, this._fns);
    };

    /**
     * Pre-compile Hemlock source code into a cached script.
     * The script can be executed repeatedly in any context without re-parsing.
     *
     * @param {string} source - Hemlock source code to compile
     * @returns {HemlockScript} A compiled script handle, or throws on failure
     *
     * @example
     * var ctx = hemlock.createContext();
     * var tick = hemlock.compile('print("tick");');
     * tick.run(ctx);  // executes without re-parsing
     * tick.run(ctx);  // same AST, no re-parse
     * tick.free();
     * ctx.destroy();
     */
    Hemlock.prototype.compile = function(source) {
        var handle = this._fns.compile(source);
        if (handle === 0) {
            throw new Error('Failed to compile script (parse error or table full)');
        }
        return new HemlockScript(handle, this._fns);
    };

    /**
     * Get the raw Emscripten Module object.
     * Useful for advanced operations not covered by this API.
     *
     * @returns {object} The Emscripten Module
     */
    Hemlock.prototype.getModule = function() {
        return this._module;
    };

    /* ====================================================================
     * Export
     * ==================================================================== */

    // Export for different environments
    if (typeof module !== 'undefined' && module.exports) {
        // Node.js / CommonJS
        module.exports = { Hemlock: Hemlock, HemlockContext: HemlockContext, HemlockScript: HemlockScript };
    } else if (typeof define === 'function' && define.amd) {
        // AMD
        define(function() { return { Hemlock: Hemlock, HemlockContext: HemlockContext, HemlockScript: HemlockScript }; });
    } else {
        // Browser global
        root.Hemlock = Hemlock;
        root.HemlockContext = HemlockContext;
        root.HemlockScript = HemlockScript;
    }

})(typeof globalThis !== 'undefined' ? globalThis :
   typeof self !== 'undefined' ? self :
   typeof window !== 'undefined' ? window :
   typeof global !== 'undefined' ? global : this);
