/*
 * onnx_wrapper.c - Flat C wrapper for ONNX Runtime's OrtApi struct
 *
 * ONNX Runtime uses a struct-of-function-pointers pattern (OrtApi) accessed
 * via OrtGetApiBase(). This wrapper flattens that into simple exported C
 * functions that Hemlock can call through its FFI system.
 *
 * Build:
 *   gcc -shared -fPIC -o libhemlock_onnx.so onnx_wrapper.c -lonnxruntime
 *
 * Requires:
 *   - libonnxruntime.so (ONNX Runtime shared library)
 *   - onnxruntime_c_api.h (ONNX Runtime C API header)
 *
 * Install ONNX Runtime:
 *   - From GitHub releases: https://github.com/microsoft/onnxruntime/releases
 *   - Extract and copy:
 *       sudo cp lib/libonnxruntime.so* /usr/local/lib/
 *       sudo cp include/* /usr/local/include/
 *       sudo ldconfig
 */

#include <onnxruntime_c_api.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========================================================================== */
/* Internal State                                                             */
/* ========================================================================== */

static const OrtApi* g_ort = NULL;

static int ensure_api(void) {
    if (g_ort) return 0;
    const OrtApiBase* base = OrtGetApiBase();
    if (!base) return -1;
    g_ort = base->GetApi(ORT_API_VERSION);
    return g_ort ? 0 : -1;
}

/*
 * Error handling pattern (matches Hemlock's vector module):
 *   - Functions take a char** err parameter
 *   - On error, *err is set to a strdup'd error message
 *   - On success, *err is left unchanged (caller should init to NULL)
 *   - Caller is responsible for freeing *err
 */
static int check_status(OrtStatus* status, char** err) {
    if (status == NULL) return 0;  /* success */
    if (g_ort && err) {
        const char* msg = g_ort->GetErrorMessage(status);
        *err = msg ? strdup(msg) : strdup("unknown ONNX Runtime error");
    }
    if (g_ort) g_ort->ReleaseStatus(status);
    return -1;
}

/* ========================================================================== */
/* Version & Initialization                                                   */
/* ========================================================================== */

/* Returns the ONNX Runtime version string (e.g., "1.17.0") */
const char* hml_ort_version(void) {
    const OrtApiBase* base = OrtGetApiBase();
    if (!base) return "unknown";
    return base->GetVersionString();
}

/* Returns the ORT API version number this wrapper was built against */
int hml_ort_api_version(void) {
    return ORT_API_VERSION;
}

/* ========================================================================== */
/* Environment                                                                */
/* ========================================================================== */

/* Create an ORT environment
 * log_level: 0=verbose, 1=info, 2=warning, 3=error, 4=fatal
 * logid: identifier string for logging
 * err: error output
 * Returns: OrtEnv* or NULL on error
 */
OrtEnv* hml_ort_create_env(int log_level, const char* logid, char** err) {
    if (ensure_api() != 0) {
        if (err) *err = strdup("failed to initialize ONNX Runtime API");
        return NULL;
    }
    OrtEnv* env = NULL;
    if (check_status(g_ort->CreateEnv((OrtLoggingLevel)log_level, logid, &env), err)) {
        return NULL;
    }
    return env;
}

void hml_ort_release_env(OrtEnv* env) {
    if (g_ort && env) g_ort->ReleaseEnv(env);
}

/* ========================================================================== */
/* Session Options                                                            */
/* ========================================================================== */

OrtSessionOptions* hml_ort_create_session_options(char** err) {
    if (ensure_api() != 0) {
        if (err) *err = strdup("failed to initialize ONNX Runtime API");
        return NULL;
    }
    OrtSessionOptions* opts = NULL;
    if (check_status(g_ort->CreateSessionOptions(&opts), err)) {
        return NULL;
    }
    return opts;
}

void hml_ort_release_session_options(OrtSessionOptions* opts) {
    if (g_ort && opts) g_ort->ReleaseSessionOptions(opts);
}

int hml_ort_session_options_set_intra_threads(OrtSessionOptions* opts, int num, char** err) {
    if (!g_ort || !opts) return -1;
    return check_status(g_ort->SetIntraOpNumThreads(opts, num), err);
}

int hml_ort_session_options_set_inter_threads(OrtSessionOptions* opts, int num, char** err) {
    if (!g_ort || !opts) return -1;
    return check_status(g_ort->SetInterOpNumThreads(opts, num), err);
}

/* Graph optimization level: 0=disable, 1=basic, 2=extended, 99=all */
int hml_ort_session_options_set_graph_opt(OrtSessionOptions* opts, int level, char** err) {
    if (!g_ort || !opts) return -1;
    return check_status(
        g_ort->SetSessionGraphOptimizationLevel(opts, (GraphOptimizationLevel)level), err);
}

/* ========================================================================== */
/* Session                                                                    */
/* ========================================================================== */

/* Create a session from a model file path */
OrtSession* hml_ort_create_session(OrtEnv* env, const char* model_path,
                                    OrtSessionOptions* opts, char** err) {
    if (!g_ort || !env) {
        if (err) *err = strdup("invalid environment");
        return NULL;
    }
    OrtSession* session = NULL;
    if (check_status(g_ort->CreateSession(env, model_path, opts, &session), err)) {
        return NULL;
    }
    return session;
}

/* Create a session from a model buffer in memory */
OrtSession* hml_ort_create_session_from_array(OrtEnv* env, const void* model_data,
                                               size_t model_data_len,
                                               OrtSessionOptions* opts, char** err) {
    if (!g_ort || !env) {
        if (err) *err = strdup("invalid environment");
        return NULL;
    }
    OrtSession* session = NULL;
    if (check_status(
            g_ort->CreateSessionFromArray(env, model_data, model_data_len, opts, &session), err)) {
        return NULL;
    }
    return session;
}

void hml_ort_release_session(OrtSession* session) {
    if (g_ort && session) g_ort->ReleaseSession(session);
}

/* ========================================================================== */
/* Model Inspection                                                           */
/* ========================================================================== */

/* Get number of model inputs */
size_t hml_ort_session_input_count(OrtSession* session, char** err) {
    if (!g_ort || !session) return 0;
    size_t count = 0;
    check_status(g_ort->SessionGetInputCount(session, &count), err);
    return count;
}

/* Get number of model outputs */
size_t hml_ort_session_output_count(OrtSession* session, char** err) {
    if (!g_ort || !session) return 0;
    size_t count = 0;
    check_status(g_ort->SessionGetOutputCount(session, &count), err);
    return count;
}

/* Get input name by index. Returns malloc'd string; caller must call hml_ort_free_name(). */
char* hml_ort_session_input_name(OrtSession* session, size_t idx, char** err) {
    if (!g_ort || !session) return NULL;
    OrtAllocator* allocator = NULL;
    if (check_status(g_ort->GetAllocatorWithDefaultOptions(&allocator), err)) return NULL;
    char* name = NULL;
    if (check_status(g_ort->SessionGetInputName(session, idx, allocator, &name), err)) return NULL;
    /* Copy to a standard malloc'd string so we can free with free() */
    char* copy = strdup(name);
    g_ort->AllocatorFree(allocator, name);
    return copy;
}

/* Get output name by index. Returns malloc'd string; caller must call hml_ort_free_name(). */
char* hml_ort_session_output_name(OrtSession* session, size_t idx, char** err) {
    if (!g_ort || !session) return NULL;
    OrtAllocator* allocator = NULL;
    if (check_status(g_ort->GetAllocatorWithDefaultOptions(&allocator), err)) return NULL;
    char* name = NULL;
    if (check_status(g_ort->SessionGetOutputName(session, idx, allocator, &name), err)) return NULL;
    char* copy = strdup(name);
    g_ort->AllocatorFree(allocator, name);
    return copy;
}

void hml_ort_free_name(char* name) {
    free(name);
}

/* Get input tensor shape. Writes dimensions to shape_out, returns number of dims.
 * shape_out must be pre-allocated with at least max_dims entries.
 * Returns -1 on error. */
int64_t hml_ort_session_input_shape(OrtSession* session, size_t idx,
                                     int64_t* shape_out, size_t max_dims, char** err) {
    if (!g_ort || !session) return -1;
    OrtTypeInfo* type_info = NULL;
    if (check_status(g_ort->SessionGetInputTypeInfo(session, idx, &type_info), err)) return -1;

    const OrtTensorTypeAndShapeInfo* tensor_info = NULL;
    if (check_status(g_ort->CastTypeInfoToTensorInfo(type_info, &tensor_info), err)) {
        g_ort->ReleaseTypeInfo(type_info);
        return -1;
    }

    size_t ndims = 0;
    if (check_status(g_ort->GetDimensionsCount(tensor_info, &ndims), err)) {
        g_ort->ReleaseTypeInfo(type_info);
        return -1;
    }

    if (ndims > max_dims) ndims = max_dims;
    if (check_status(g_ort->GetDimensions(tensor_info, shape_out, ndims), err)) {
        g_ort->ReleaseTypeInfo(type_info);
        return -1;
    }

    g_ort->ReleaseTypeInfo(type_info);
    return (int64_t)ndims;
}

/* Get output tensor shape */
int64_t hml_ort_session_output_shape(OrtSession* session, size_t idx,
                                      int64_t* shape_out, size_t max_dims, char** err) {
    if (!g_ort || !session) return -1;
    OrtTypeInfo* type_info = NULL;
    if (check_status(g_ort->SessionGetOutputTypeInfo(session, idx, &type_info), err)) return -1;

    const OrtTensorTypeAndShapeInfo* tensor_info = NULL;
    if (check_status(g_ort->CastTypeInfoToTensorInfo(type_info, &tensor_info), err)) {
        g_ort->ReleaseTypeInfo(type_info);
        return -1;
    }

    size_t ndims = 0;
    if (check_status(g_ort->GetDimensionsCount(tensor_info, &ndims), err)) {
        g_ort->ReleaseTypeInfo(type_info);
        return -1;
    }

    if (ndims > max_dims) ndims = max_dims;
    if (check_status(g_ort->GetDimensions(tensor_info, shape_out, ndims), err)) {
        g_ort->ReleaseTypeInfo(type_info);
        return -1;
    }

    g_ort->ReleaseTypeInfo(type_info);
    return (int64_t)ndims;
}

/* Get input element type (returns ONNXTensorElementDataType enum value) */
int hml_ort_session_input_type(OrtSession* session, size_t idx, char** err) {
    if (!g_ort || !session) return -1;
    OrtTypeInfo* type_info = NULL;
    if (check_status(g_ort->SessionGetInputTypeInfo(session, idx, &type_info), err)) return -1;

    const OrtTensorTypeAndShapeInfo* tensor_info = NULL;
    if (check_status(g_ort->CastTypeInfoToTensorInfo(type_info, &tensor_info), err)) {
        g_ort->ReleaseTypeInfo(type_info);
        return -1;
    }

    ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    if (check_status(g_ort->GetTensorElementType(tensor_info, &type), err)) {
        g_ort->ReleaseTypeInfo(type_info);
        return -1;
    }

    g_ort->ReleaseTypeInfo(type_info);
    return (int)type;
}

/* Get output element type */
int hml_ort_session_output_type(OrtSession* session, size_t idx, char** err) {
    if (!g_ort || !session) return -1;
    OrtTypeInfo* type_info = NULL;
    if (check_status(g_ort->SessionGetOutputTypeInfo(session, idx, &type_info), err)) return -1;

    const OrtTensorTypeAndShapeInfo* tensor_info = NULL;
    if (check_status(g_ort->CastTypeInfoToTensorInfo(type_info, &tensor_info), err)) {
        g_ort->ReleaseTypeInfo(type_info);
        return -1;
    }

    ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    if (check_status(g_ort->GetTensorElementType(tensor_info, &type), err)) {
        g_ort->ReleaseTypeInfo(type_info);
        return -1;
    }

    g_ort->ReleaseTypeInfo(type_info);
    return (int)type;
}

/* ========================================================================== */
/* Memory Info                                                                */
/* ========================================================================== */

OrtMemoryInfo* hml_ort_create_cpu_memory_info(char** err) {
    if (ensure_api() != 0) {
        if (err) *err = strdup("failed to initialize ONNX Runtime API");
        return NULL;
    }
    OrtMemoryInfo* info = NULL;
    if (check_status(
            g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &info), err)) {
        return NULL;
    }
    return info;
}

void hml_ort_release_memory_info(OrtMemoryInfo* info) {
    if (g_ort && info) g_ort->ReleaseMemoryInfo(info);
}

/* ========================================================================== */
/* Tensor Creation                                                            */
/* ========================================================================== */

/*
 * Create a tensor wrapping user-provided data.
 * The data buffer must remain valid for the lifetime of the tensor.
 *
 * data: pointer to contiguous element data
 * data_len: size of data in bytes
 * shape: pointer to int64_t array of dimension sizes
 * ndims: number of dimensions
 * element_type: ONNXTensorElementDataType enum value
 * mem_info: CPU memory info (from hml_ort_create_cpu_memory_info)
 * err: error output
 */
OrtValue* hml_ort_create_tensor(void* data, size_t data_len,
                                 const int64_t* shape, size_t ndims,
                                 int element_type,
                                 OrtMemoryInfo* mem_info, char** err) {
    if (!g_ort || !mem_info) {
        if (err) *err = strdup("invalid memory info or uninitialized API");
        return NULL;
    }
    OrtValue* value = NULL;
    if (check_status(
            g_ort->CreateTensorWithDataAsOrtValue(
                mem_info, data, data_len, shape, ndims,
                (ONNXTensorElementDataType)element_type, &value), err)) {
        return NULL;
    }
    return value;
}

void hml_ort_release_value(OrtValue* value) {
    if (g_ort && value) g_ort->ReleaseValue(value);
}

/* ========================================================================== */
/* Tensor Inspection                                                          */
/* ========================================================================== */

/* Get pointer to raw tensor data */
void* hml_ort_tensor_data(OrtValue* value, char** err) {
    if (!g_ort || !value) return NULL;
    void* data = NULL;
    check_status(g_ort->GetTensorMutableData(value, &data), err);
    return data;
}

/* Get number of dimensions */
int64_t hml_ort_tensor_ndims(OrtValue* value, char** err) {
    if (!g_ort || !value) return -1;
    OrtTensorTypeAndShapeInfo* info = NULL;
    if (check_status(g_ort->GetTensorTypeAndShape(value, &info), err)) return -1;
    size_t ndims = 0;
    int rc = check_status(g_ort->GetDimensionsCount(info, &ndims), err);
    g_ort->ReleaseTensorTypeAndShapeInfo(info);
    return rc ? -1 : (int64_t)ndims;
}

/* Get tensor shape. shape_out must be pre-allocated. Returns 0 on success. */
int hml_ort_tensor_shape(OrtValue* value, int64_t* shape_out, size_t max_dims, char** err) {
    if (!g_ort || !value) return -1;
    OrtTensorTypeAndShapeInfo* info = NULL;
    if (check_status(g_ort->GetTensorTypeAndShape(value, &info), err)) return -1;

    size_t ndims = 0;
    if (check_status(g_ort->GetDimensionsCount(info, &ndims), err)) {
        g_ort->ReleaseTensorTypeAndShapeInfo(info);
        return -1;
    }
    if (ndims > max_dims) ndims = max_dims;
    int rc = check_status(g_ort->GetDimensions(info, shape_out, ndims), err);
    g_ort->ReleaseTensorTypeAndShapeInfo(info);
    return rc;
}

/* Get total number of elements in tensor */
int64_t hml_ort_tensor_element_count(OrtValue* value, char** err) {
    if (!g_ort || !value) return -1;
    OrtTensorTypeAndShapeInfo* info = NULL;
    if (check_status(g_ort->GetTensorTypeAndShape(value, &info), err)) return -1;
    size_t count = 0;
    int rc = check_status(g_ort->GetTensorElementCount(info, &count), err);
    g_ort->ReleaseTensorTypeAndShapeInfo(info);
    return rc ? -1 : (int64_t)count;
}

/* Get tensor element type (returns ONNXTensorElementDataType) */
int hml_ort_tensor_element_type(OrtValue* value, char** err) {
    if (!g_ort || !value) return -1;
    OrtTensorTypeAndShapeInfo* info = NULL;
    if (check_status(g_ort->GetTensorTypeAndShape(value, &info), err)) return -1;
    ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    int rc = check_status(g_ort->GetTensorElementType(info, &type), err);
    g_ort->ReleaseTensorTypeAndShapeInfo(info);
    return rc ? -1 : (int)type;
}

/* ========================================================================== */
/* Inference                                                                  */
/* ========================================================================== */

/*
 * Run inference.
 *
 * session: ORT session
 * input_names: array of C strings (input tensor names)
 * input_values: array of OrtValue* (input tensors)
 * num_inputs: number of inputs
 * output_names: array of C strings (output tensor names)
 * num_outputs: number of outputs
 * output_values: pre-allocated array of OrtValue* to receive outputs
 * err: error output
 *
 * Returns 0 on success, -1 on error.
 * On success, output_values[] is filled with OrtValue* pointers owned by the caller.
 */
int hml_ort_run(OrtSession* session,
                const char* const* input_names, const OrtValue* const* input_values,
                size_t num_inputs,
                const char* const* output_names, size_t num_outputs,
                OrtValue** output_values, char** err) {
    if (!g_ort || !session) {
        if (err) *err = strdup("invalid session");
        return -1;
    }
    return check_status(
        g_ort->Run(session, NULL, input_names, input_values, num_inputs,
                   output_names, num_outputs, output_values), err);
}
