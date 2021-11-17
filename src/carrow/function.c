
#include <stdlib.h>
#include <errno.h>

#include "function.h"
#include "schema.h"
#include "array.h"
#include "vector.h"
#include "status.h"

void arrow_function_release_internal(struct ArrowFunction* function) {
  if (function == NULL || function->release == NULL) {
    return;
  }

  if (function->private_data != NULL) {
    free(function->private_data);
  }

  function->release = NULL;
}

const char* arrow_function_identity_get_last_error(struct ArrowFunction* function) {
  return NULL;
}

int arrow_function_identity_compute_ptype(struct ArrowFunction* function, int64_t n_arguments,
    struct ArrowSchema** argument_schemas, struct ArrowArray** argument_ptypes,
    struct ArrowSchema* schema_out, struct ArrowArray* ptype_out) {
  struct ArrowStatus* status = (struct ArrowStatus*) (function->private_data);
  arrow_status_reset(status);

  if (n_arguments != 1) {
    arrow_status_set_error(status, EINVAL, "Expected one argument but found %ld", n_arguments);
    RETURN_IF_NOT_OK(status);
  }

  int result = arrow_schema_copy(schema_out, argument_schemas[0]);
  if (result != 0) {
    return result;
  }

  result = arrow_array_copy_ptype(ptype_out, argument_ptypes[0]);
  if (result != 0) {
    return result;
  }

  return 0;
}

int arrow_function_identity_compute(struct ArrowFunction* function, int64_t n_arguments,
    struct ArrowSchema** argument_schemas, struct ArrowArray** argument_arrays,
    struct ArrowSchema* allocated_schema, struct ArrowArray* allocated_array) {
  struct ArrowStatus* status = (struct ArrowStatus*) (function->private_data);
  arrow_status_reset(status);

  if (n_arguments != 1) {
    arrow_status_set_error(status, EINVAL, "Expected one argument but found %ld", n_arguments);
    RETURN_IF_NOT_OK(status);
  }



  struct ArrowVector vector_src;
  arrow_vector_init(&vector_src, argument_schemas[0], argument_arrays[0], status);
  RETURN_IF_NOT_OK(status);

  struct ArrowVector vector_dest;
  arrow_vector_init(&vector_dest, allocated_schema, allocated_array, status);
  RETURN_IF_NOT_OK(status);

  arrow_vector_copy(
    &vector_dest, 0,
    &vector_src, argument_arrays[0]->offset,
    allocated_array->length,
    status
  );
  RETURN_IF_NOT_OK(status);

  return 0;
}

int arrow_function_identity(struct ArrowFunction* out) {
  out->flags = 0;
  out->compute_ptype = &arrow_function_identity_compute_ptype;
  out->compute = &arrow_function_identity_compute;
  out->get_last_error = &arrow_function_identity_get_last_error;
  out->release = &arrow_function_release_internal;
  out->private_data = malloc(sizeof(struct ArrowStatus));
  if (out->private_data == NULL) {
    return ENOMEM;
  }

  return 0;
}

int arrow_function_call(struct ArrowFunction* function, int64_t n_arguments,
                        struct ArrowSchema** argument_schemas, struct ArrowArray** argument_arrays,
                        struct ArrowSchema* schema_out, struct ArrowArray* array_out) {

  struct ArrowSchema ptype_schema;
  ptype_schema.release = NULL;
  struct ArrowArray ptype_array;
  ptype_array.release = NULL;

  int result = function->compute_ptype(
      function, n_arguments,
      argument_schemas, argument_arrays,
      &ptype_schema, &ptype_array);
  if (result != 0) {
    if (ptype_schema.release != NULL) ptype_schema.release(&ptype_schema);
    if (ptype_array.release != NULL) ptype_array.release(&ptype_array);
    return result;
  }

  result = arrow_schema_copy(schema_out, &ptype_schema);
  if (result != 0) {
    if (ptype_schema.release != NULL) ptype_schema.release(&ptype_schema);
    if (ptype_array.release != NULL) ptype_array.release(&ptype_array);
    return result;
  }

  result = arrow_array_copy_ptype(array_out, &ptype_array);
  if (result != 0) {
    if (ptype_schema.release != NULL) ptype_schema.release(&ptype_schema);
    if (ptype_array.release != NULL) ptype_array.release(&ptype_array);
    return result;
  }

  // we're now done with the schema and array we allocated
  ptype_schema.release(&ptype_schema);
  ptype_array.release(&ptype_array);

  // use vector implementation to allocate buffers to the output
  struct ArrowVector vector;
  struct ArrowStatus status;

  arrow_vector_init(&vector, schema_out, array_out, &status);
  RETURN_IF_NOT_OK(&status);

  arrow_vector_alloc_buffers(&vector, &status);
  RETURN_IF_NOT_OK(&status);

  result = function->compute(
    function, n_arguments,
    argument_schemas, argument_arrays,
    schema_out, array_out);

  return result;
}
