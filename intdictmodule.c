#include<Python.h>
#include<math.h>
#include<stdlib.h>


#ifdef __GNUC__
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifndef __GNUC__
#define likely(x) x
#define unlikely(x) x
#endif

#ifndef __builtin_assume_aligned
#define __builtin_assume_aligned(X, Y) (X)
#endif

#define cache_line_bytes 64
const int cache_line_width = cache_line_bytes / sizeof(uint64_t);
const int cache_line_mask = cache_line_bytes / sizeof(uint64_t)  - 1;
const uint64_t empty_slot = -1;

static inline uint64_t rotl(uint64_t x, uint64_t r) {
  return ((x >> (64 - r)) | (x << r));
}
// A reduced complexity, sizeof(uint64_t) only implementation of XXHASH

#define PRIME_1 11400714785074694791ULL
#define PRIME_2 14029467366897019727ULL
#define PRIME_3  1609587929392839161ULL
#define PRIME_4  9650029242287828579ULL
#define PRIME_5  2870177450012600261ULL

inline uint64_t xxh64(uint64_t k1) {
  uint64_t h64;
  h64  = PRIME_5 + 8;

  k1 *= PRIME_2;
  k1 = rotl(k1, 31);
  k1 *= PRIME_1;
  h64 ^= k1;
  h64 = rotl(h64, 27) * PRIME_1 + PRIME_4;
  h64 ^= h64 >> 33;
  h64 *= PRIME_2;
  h64 ^= h64 >> 29;
  h64 *= PRIME_3;
  h64 ^= h64 >> 32;
  return h64;
}

#define STOP -1 

typedef struct {
  uint64_t mask;
  uint64_t limit;
  uint64_t count;
  int64_t *keys;
  PyObject **values;
} intdict_t;

typedef struct _intdict_object IntdictObject;
struct _intdict_object {
  PyObject HEAD;
  intdict_t *intdict;
};


typedef uint64_t key_cluster __attribute__ ((vector_size (64)));

intdict_t *intdict_create();
void intdict_destroy(intdict_t* intdict);
int intdict_add(intdict_t* intdict, uint64_t key, PyObject *value);
int intdict_test(intdict_t *intdict, uint64_t uuid);
PyObject *intdict_value(intdict_t *intdict, uint64_t uuid);
uint64_t intdict_physical_offset(uint64_t intended_slot, uint64_t step, uint64_t mask);

static inline void *cache_aligned_calloc(size_t nmemb, size_t size, int64_t value) {
  int64_t *retval = NULL;
  posix_memalign(&retval, cache_line_bytes, nmemb * size);
  if (!value) {
    memset(retval, 0, nmemb * size);
  } else {
    uint64_t i=0;
    for(i=0; i< (size*nmemb) / sizeof(uint64_t); i++)
      retval[i] = value;
  }
  return retval;
}

void intdict_destroy(intdict_t *intdict) {
  int i=0;
  for(i=0; i <= intdict->mask; i++)
    if (intdict->values[i])
      Py_DECREF(intdict->values[i]);
  free(intdict->keys);
  free(intdict->values);
  free(intdict);
}

intdict_t*  intdict_create(uint64_t initial_size) {
  intdict_t *intdict;
  intdict = malloc(sizeof(intdict_t));
  if (!intdict)
    goto error;
  if (initial_size < cache_line_bytes / ( sizeof(PyObject *) * 2))
    initial_size = cache_line_bytes / ( sizeof(PyObject *) * 2);
  initial_size = (initial_size * 5) / 4;
  int power_of_two = 1;
  while (initial_size) {
    power_of_two *= 2;
    initial_size /= 2;
  }
  intdict->mask = power_of_two - 1;
  intdict->limit = (power_of_two / 5) * 4;

  intdict->count = 0;
  if (!(intdict->keys = cache_aligned_calloc(sizeof(uint64_t), intdict->mask + 1, -1)))
    goto error;
  if (!(intdict->values = cache_aligned_calloc(sizeof(PyObject*), intdict->mask + 1, 0)))
    goto error;
  return intdict;

error:
  intdict_destroy(intdict);
  return NULL;
}


static inline uint64_t physical_offset(uint64_t intended_slot, uint64_t masked_intended_slot, uint64_t step, uint64_t mask) {
  return ((masked_intended_slot | ((intended_slot + step)&cache_line_mask)) + (step & ~cache_line_mask)) & mask;
}

static inline uint64_t partial_physical_offset(uint64_t intended_slot,  uint64_t step) {
  return  ((intended_slot + step)&cache_line_mask);
}


uint64_t intdict_physical_offset(uint64_t intended_slot, uint64_t step, uint64_t mask) {
  return physical_offset(intended_slot, intended_slot & ~cache_line_mask, step, mask);
}


static inline int intdict_find(intdict_t *intdict, uint64_t key) {
  uint64_t intended_slot = key & intdict->mask;
  uint64_t masked_intended_slot = intended_slot & ~cache_line_mask;
  int step = 0;
  uint64_t offset;
  uint64_t *temp_keys;

  while (1) {
    temp_keys = intdict->keys + masked_intended_slot;
    for(step=0; step<=cache_line_mask; step += 1) {
      offset = partial_physical_offset(intended_slot, step);
      if (temp_keys[offset] == key)
        return offset + masked_intended_slot;
      if (temp_keys[offset] == STOP)
        return -1;
    }
    intended_slot += cache_line_width;
    intended_slot &= intdict->mask;
    masked_intended_slot += cache_line_width;
    masked_intended_slot &= intdict->mask;
  }
}

int intdict_test(intdict_t *intdict, uint64_t key) {
  return intdict_find(intdict, key) != -1;
}

PyObject *intdict_value(intdict_t *intdict, uint64_t key) {
  int64_t offset = intdict_find(intdict, key);
  if (offset == -1)
    return 0;
  return intdict->values[offset];
}


inline int intdict_add(intdict_t *intdict, uint64_t key, PyObject *value) {


  Py_INCREF(value);
  if (intdict->count >= intdict->limit) {
    uint64_t new_mask = (intdict->mask << 1) | 1;
    uint64_t i;
    uint64_t *new_keys = cache_aligned_calloc(sizeof(uint64_t), new_mask + 1, -1);
    if (!new_keys) {
      return -1;
    }
    PyObject **new_values = cache_aligned_calloc(sizeof(PyObject*), new_mask + 1, 0);
    if (!new_values) {
      free(new_keys);
      return -1;
    }
    for(i=0; i<=intdict->mask; i++) {
      if (intdict->keys[i] == STOP)
        continue;
      uint64_t intended_slot = intdict->keys[i] & new_mask;

      uint64_t step = 0;
      uint64_t offset;
      uint64_t masked_intended_slot = intended_slot & ~cache_line_mask;
      key_cluster offsets;
      uint64_t *temp_new_keys;

      while (1) {
        temp_new_keys = new_keys + masked_intended_slot;
        for(step=0; step<=cache_line_mask; step += 1) {
          offset = partial_physical_offset(intended_slot, step);
          if (temp_new_keys[offset]==STOP) {
            goto found;
          }
        }
        intended_slot += cache_line_width;
        intended_slot &= intdict->mask;
        masked_intended_slot += cache_line_width;
        masked_intended_slot &= intdict->mask;
      }
found:
      temp_new_keys[offset] = intdict->keys[i];
      new_values[offset + masked_intended_slot] = intdict->values[i];
    }
    free(intdict->keys);
    free(intdict->values);
    intdict->keys = new_keys;
    intdict->values = new_values;
    intdict->mask = new_mask;
    intdict->limit = new_mask * 4 / 5;
  }
  uint64_t intended_slot = key & intdict->mask;
  uint64_t masked_intended_slot = intended_slot & ~cache_line_mask;
  uint64_t step = 0;
  uint64_t offset;
  uint64_t *temp_keys;
  while (1) {
    temp_keys = intdict->keys + masked_intended_slot;
    for(step=0; step<=cache_line_mask; step += 1) {
      offset = partial_physical_offset(intended_slot, step);
      if (temp_keys[offset]==STOP) {
        offset += masked_intended_slot;
        intdict->keys[offset] = key;
        intdict->values[offset] = value;
        intdict->count += 1;
        return 1;
      }
      if (temp_keys[offset] == key) {
	Py_DECREF(intdict->values[masked_intended_slot + offset]);
        intdict->values[masked_intended_slot + offset] = value;
        return 0;
      }
    }
    intended_slot += cache_line_width;
    intended_slot &= intdict->mask;
    masked_intended_slot += cache_line_width;
    masked_intended_slot &= intdict->mask;
  }
}


static PyObject *
intdict_clear(IntdictObject *intdict, PyObject *_) {
  intdict_t *id = intdict->intdict;
  int i=0;
  PyObject *target;
  for(i=0; i<=id->mask; i++) {
    id->keys[i] = STOP;
    if (id->values[i])
      Py_DECREF(id->values[i]);
  }
  id->count=0;
  Py_RETURN_NONE;
}

static Py_ssize_t
intdict_object_len(IntdictObject* intdict) {
  return intdict->intdict->count;
}


int intdict_object_contains(IntdictObject *self, PyObject *item) {
  intdict_t *intdict = self->intdict;
  if (!PyInt_Check(item))
    return NULL;
  long key = PyLong_AsLong(item);
  if (key == -1)
    return NULL;
  return intdict_test(intdict, key);
}

PyObject *intdict_object_getitem(IntdictObject *self, PyObject *key_obj) {
  intdict_t *intdict = self->intdict;
  if (!PyInt_Check(key_obj))  {
    goto error;
  }
  long key = PyLong_AsLong(key_obj);
  if (key == -1)
    goto error;
  int index = intdict_find(intdict, key);
  if (index == -1)
    goto error;
  Py_INCREF(intdict->values[index]);
  return intdict->values[index];
error:
  PyErr_SetObject(PyExc_KeyError, key_obj);
  return NULL;
}

int intdict_object_setitem(IntdictObject *self, PyObject *key_obj, PyObject *value) {
  intdict_t *intdict = self->intdict;
  long key=-1;
  if (!PyInt_Check(key_obj))  {
    key = PyObject_Hash(key_obj);
    if (key == -1) {
      PyErr_SetString(PyExc_TypeError, "non-int key isn't a hashable type");
      return -1;
    }
  }
  key = PyLong_AsLong(key_obj);
  if (key == -1) {
      PyErr_SetString(PyExc_TypeError, "Sorry can't store -1 or anything out of range for a long\n");
    return -1;
  }
  int retval = intdict_add(intdict, key, value);
  return retval == -1;
}

static PySequenceMethods intdict_object_mapping_methods = {
  intdict_object_len, /* mp_length lenfunc */
  intdict_object_getitem, /* mp_subscript binaryfunc */
  intdict_object_setitem, /* mp_ass_subscript objobjargproc */
};

static PySequenceMethods intdict_object_sequence_methods = {
  intdict_object_len, /* sq_length */
  0, /* sq_concat */
  0, /* sq_repeat */
  0, /* sq_item */
  0, /* sq_slice */
  0, /* sq_ass_item */
  0, /* sq_ass_slice */
  (objobjproc)intdict_object_contains,	/* sq_contains */
};

static PyMethodDef intdict_methods[] = {
  {"clear", (PyCFunction)intdict_clear, METH_O, NULL},
  {NULL, NULL}
};

static void intdict_type_dealloc(IntdictObject *intdict) {
  Py_TRASHCAN_SAFE_BEGIN(intdict);
  intdict_destroy(intdict->intdict);
  Py_TRASHCAN_SAFE_END(intdict);
}


PyObject *
make_new_intdict(PyTypeObject *type, uint64_t capacity);


static int
intdict_init(IntdictObject *self, PyObject *args, PyObject *kwargs) {
  return 0;
}


static PyObject *
intdict_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
  static char *kwlist[] = {"capacity", NULL};

  uint64_t capacity=0;
  PyArg_ParseTupleAndKeywords(args,
                              kwargs,
                              "|l",
                              kwlist,
                              &capacity);

  IntdictObject *obj = make_new_intdict(type, capacity);
  if (!obj)
    PyErr_NoMemory();
  return (PyObject *)obj;
}

PyTypeObject IntdictType = {
  PyVarObject_HEAD_INIT(&PyType_Type, 0)
  "Intdict", /* tp_name */
  sizeof(IntdictObject), /* tp_basicsize */
  0, /* tp_itemsize */
  (destructor)intdict_type_dealloc, /* tp_dealloc */
  0, /* tp_print */
  0, /* tp_getattr */
  0, /* tp_setattr */
  0, /* tp_cmp */
  0, /* tp_repr */
  0, /* tp_as_number */
  &intdict_object_sequence_methods, /* tp_as_seqeunce */
  &intdict_object_mapping_methods, /* tp_mapping */
  (hashfunc)PyObject_HashNotImplemented, /*tp_hash */
  0, /* tp_call */
  0, /* tp_str */
  PyObject_GenericGetAttr, /* tp_getattro */
  0, /* tp_setattro */
  0, /* tp_as_buffer */
  Py_TPFLAGS_HAVE_SEQUENCE_IN,	/* tp_flags */
  0, /* tp_doc */
  0, /* tp_traverse */
  0, /* tp_clear */
  0, /* tp_richcompare */
  0, /* tp_weaklistoffset */
  0, /* tp_iter */
  0, /* tp_iternext */
  intdict_methods, /* tp_methods */
  0, /* tp_members */
  0, /* tp_genset */
  0, /* tp_base */
  0, /* tp_dict */
  0, /* tp_descr_get */
  0, /* tp_descr_set */
  0, /* tp_dictoffset */
  (initproc)intdict_init, /* tp_init */
  PyType_GenericAlloc, /* tp_alloc */
  intdict_new, /* tp_new */
  0,
};



PyObject *
make_new_intdict(PyTypeObject *type, uint64_t initial_size) {
  IntdictObject *intdict = PyObject_GC_New(IntdictObject, &IntdictType);;
  Py_INCREF(intdict);
  if (!intdict)
    return NULL;
  if (!(intdict->intdict = intdict_create(initial_size))) {
    return NULL;
  }
  return intdict;
}


static PyMethodDef intdictmodule_methods[] = {
  {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC
initintdict(void) {
  PyObject *m = Py_InitModule("intdict", intdictmodule_methods);
  Py_INCREF(&IntdictType);
  PyModule_AddObject(m, "intdict", (PyObject *)&IntdictType);
};
