#include "builtin.h"
#include "machine.h"
#include "constants.h"
#include "run.h"

using namespace vm;

namespace {

object
doInvoke(Thread* t, object this_, object instance, object arguments)
{
  object v = pushReference(t, run2(t, this_, instance, arguments));
  if (t->exception) {
    t->exception = makeInvocationTargetException(t, t->exception);
  }
  return v;
}

inline void
replace(char a, char b, char* c)
{
  for (; *c; ++c) if (*c == a) *c = b;
}

jstring
Object_toString(Thread* t, jobject this_)
{
  object s = makeString
    (t, "%s@%p",
     &byteArrayBody(t, className(t, objectClass(t, *this_)), 0),
     *this_);

  return pushReference(t, s);
}

jclass
Object_getClass(Thread* t, jobject this_)
{
  return pushReference(t, objectClass(t, *this_));
}

void
Object_wait(Thread* t, jobject this_, jlong milliseconds)
{
  vm::wait(t, *this_, milliseconds);
}

void
Object_notify(Thread* t, jobject this_)
{
  notify(t, *this_);
}

void
Object_notifyAll(Thread* t, jobject this_)
{
  notifyAll(t, *this_);
}

jint
Object_hashCode(Thread* t, jobject this_)
{
  return objectHash(t, *this_);
}

jclass
ClassLoader_defineClass(Thread* t, jclass, jbyteArray b, jint offset,
                        jint length)
{
  uint8_t* buffer = static_cast<uint8_t*>(t->vm->system->allocate(length));
  memcpy(buffer, &byteArrayBody(t, *b, offset), length);
  object c = parseClass(t, buffer, length);
  t->vm->system->free(buffer);
  return pushReference(t, c);
}

jclass
search(Thread* t, jstring name, object (*op)(Thread*, object),
       bool replaceDots)
{
  if (LIKELY(name)) {
    object n = makeByteArray(t, stringLength(t, *name) + 1, false);
    char* s = reinterpret_cast<char*>(&byteArrayBody(t, n, 0));
    stringChars(t, *name, s);
    
    if (replaceDots) {
      replace('.', '/', s);
    }

    object r = op(t, n);
    if (t->exception) {
      return 0;
    }

    return pushReference(t, r);
  } else {
    t->exception = makeNullPointerException(t);
    return 0;
  }
}

jclass
SystemClassLoader_findLoadedClass(Thread* t, jclass, jstring name)
{
  return search(t, name, findLoadedClass, true);
}

jclass
SystemClassLoader_findClass(Thread* t, jclass, jstring name)
{
  return search(t, name, resolveClass, true);
}

jboolean
SystemClassLoader_resourceExists(Thread* t, jclass, jstring name)
{
  if (LIKELY(name)) {
    char n[stringLength(t, *name) + 1];
    stringChars(t, *name, n);
    return t->vm->finder->exists(n);
  } else {
    t->exception = makeNullPointerException(t);
    return 0;
  }
}

jobject
ObjectInputStream_makeInstance(Thread* t, jclass, jclass c)
{
  return pushReference(t, make(t, *c));
}

jclass
Class_primitiveClass(Thread* t, jclass, jchar name)
{
  switch (name) {
  case 'B':
    return pushReference(t, arrayBody(t, t->vm->types, Machine::JbyteType));
  case 'C':
    return pushReference(t, arrayBody(t, t->vm->types, Machine::JcharType));
  case 'D':
    return pushReference(t, arrayBody(t, t->vm->types, Machine::JdoubleType));
  case 'F':
    return pushReference(t, arrayBody(t, t->vm->types, Machine::JfloatType));
  case 'I':
    return pushReference(t, arrayBody(t, t->vm->types, Machine::JintType));
  case 'J':
    return pushReference(t, arrayBody(t, t->vm->types, Machine::JlongType));
  case 'S':
    return pushReference(t, arrayBody(t, t->vm->types, Machine::JshortType));
  case 'V':
    return pushReference(t, arrayBody(t, t->vm->types, Machine::JvoidType));
  case 'Z':
    return pushReference(t, arrayBody(t, t->vm->types, Machine::JbooleanType));
  default:
    t->exception = makeIllegalArgumentException(t);
    return 0;
  }
}

void
Class_initialize(Thread* t, jobject this_)
{
  acquire(t, t->vm->classLock);
  object c = *this_;
  if (classVmFlags(t, c) & NeedInitFlag
      and (classVmFlags(t, c) & InitFlag) == 0)
  {
    classVmFlags(t, c) |= InitFlag;
    run(t, classInitializer(t, c), 0);
  } else {
    release(t, t->vm->classLock);
  }
}

jboolean
Class_isAssignableFrom(Thread* t, jobject this_, jclass that)
{
  if (LIKELY(that)) {
    return vm::isAssignableFrom(t, *this_, *that);
  } else {
    t->exception = makeNullPointerException(t);
    return 0;
  }
}

jobject
Field_get(Thread* t, jobject this_, jobject instancep)
{
  object field = *this_;

  if (fieldFlags(t, field) & ACC_STATIC) {
    object v = arrayBody(t, classStaticTable(t, fieldClass(t, field)),
                         fieldOffset(t, field));

    switch (fieldCode(t, field)) {
    case ByteField:
      return pushReference(t, makeByte(t, intValue(t, v)));

    case BooleanField:
      return pushReference(t, makeBoolean(t, intValue(t, v)));

    case CharField:
      return pushReference(t, makeChar(t, intValue(t, v)));

    case ShortField:
      return pushReference(t, makeShort(t, intValue(t, v)));

    case FloatField:
      return pushReference(t, makeFloat(t, intValue(t, v)));

    case DoubleField:
      return pushReference(t, makeDouble(t, longValue(t, v)));

    case IntField:
    case LongField:
    case ObjectField:
      return pushReference(t, v);

    default:
      abort(t);
    }
  } else if (instancep) {
    object instance = *instancep;

    if (instanceOf(t, fieldClass(t, this_), instance)) {
      switch (fieldCode(t, field)) {
      case ByteField:
        return pushReference
          (t, makeByte(t, cast<int8_t>(instance, fieldOffset(t, field))));

      case BooleanField:
        return pushReference
          (t, makeBoolean(t, cast<uint8_t>(instance, fieldOffset(t, field))));

      case CharField:
        return pushReference
          (t, makeChar(t, cast<uint16_t>(instance, fieldOffset(t, field))));

      case ShortField:
        return pushReference
          (t, makeShort(t, cast<int16_t>(instance, fieldOffset(t, field))));

      case FloatField:
        return pushReference
          (t, makeFloat(t, cast<uint32_t>(instance, fieldOffset(t, field))));

      case IntField:
        return pushReference
          (t, makeInt(t, cast<int32_t>(instance, fieldOffset(t, field))));

      case DoubleField:
        return pushReference
          (t, makeDouble(t, cast<uint64_t>(instance, fieldOffset(t, field))));

      case LongField:
        return pushReference
          (t, makeLong(t, cast<int64_t>(instance, fieldOffset(t, field))));

      case ObjectField:
        return pushReference
          (t, cast<object>(instance, fieldOffset(t, field)));

      default:
        abort(t);
      }
    } else {
      t->exception = makeIllegalArgumentException(t);
    }
  } else {
    t->exception = makeNullPointerException(t);
  }

  return 0;
}

void
Field_set(Thread* t, jobject this_, jobject instancep, jobject value)
{
  object field = *this_;
  object v = (value ? *value : 0);

  if (fieldFlags(t, field) & ACC_STATIC) {
    object* p = &arrayBody(t, classStaticTable(t, fieldClass(t, field)),
                           fieldOffset(t, field));

    if (fieldCode(t, field) == ObjectField or v) {
      switch (fieldCode(t, field)) {
      case ByteField:
        set(t, *p, makeInt(t, byteValue(t, v)));
        break;

      case BooleanField:
        set(t, *p, makeInt(t, booleanValue(t, v)));
        break;

      case CharField:
        set(t, *p, makeInt(t, charValue(t, v)));
        break;

      case ShortField:
        set(t, *p, makeInt(t, shortValue(t, v)));
        break;

      case FloatField:
        set(t, *p, makeInt(t, floatValue(t, v)));
        break;

      case DoubleField:
        set(t, *p, makeLong(t, longValue(t, v)));
        break;

      case IntField:
      case LongField:
      case ObjectField:
        set(t, *p, v);
        break;

      default:
        abort(t);
      }
    } else {
      t->exception = makeNullPointerException(t);
    }
  } else if (instancep) {
    object instance = *instancep;

    if (instanceOf(t, fieldClass(t, this_), instance)) {
      switch (fieldCode(t, field)) {
      case ObjectField:
        set(t, cast<object>(instance, fieldOffset(t, field)), v);
        break;

      default: {
          uint8_t* body = &cast<uint8_t>(instance, fieldOffset(t, field));
          if (v) {
            memcpy(body, &cast<uint8_t>(v, BytesPerWord),
                   primitiveSize(t, fieldCode(t, field)));
          } else {
            t->exception = makeNullPointerException(t);
          }
      } break;
      }
    } else {
      t->exception = makeIllegalArgumentException(t);
    }
  } else {
    t->exception = makeNullPointerException(t);
  }
}

jobject
Constructor_make(Thread* t, jclass, jclass c)
{
  return pushReference(t, make(t, c));
}

jobject
Method_getCaller(Thread* t, jclass)
{
  return pushReference
    (t, frameMethod(t, frameNext(t, frameNext(t, t->frame))));
}

jobject
Method_invoke(Thread* t, jobject this_, jobject instancep,
              jobjectArray argumentsp)
{
  object method = *this_;

  if (argumentsp) {
    object arguments = *argumentsp;

    if (methodFlags(t, method) & ACC_STATIC) {
      if (objectArrayLength(t, arguments)
          == methodParameterCount(t, method))
      {
        return pushReference(t, doInvoke(t, method, 0, arguments));
      } else {
        t->exception = makeArrayIndexOutOfBoundsException(t, 0);
      }
    } else if (instancep) {
      object instance = *instancep;

      if (instanceOf(t, methodClass(t, method), instance)) {
        if (objectArrayLength(t, arguments)
            == static_cast<unsigned>(methodParameterCount(t, method) - 1))
        {
          return pushReference(t, doInvoke(t, method, instance, arguments));
        } else {
          t->exception = makeArrayIndexOutOfBoundsException(t, 0);
        }
      }
    } else {
      t->exception = makeNullPointerException(t);
    }
  } else {
    t->exception = makeNullPointerException(t);
  }

  return 0;
}

jobject
Array_get(Thread* t, jobject array, int index)
{
  if (LIKELY(array)) {
    object a = *array;
    unsigned elementSize = classArrayElementSize(t, objectClass(t, a));

    if (LIKELY(elementSize)) {
      intptr_t length = cast<uintptr_t>(a, BytesPerWord);

      if (LIKELY(index >= 0 and index < length)) {
        switch (byteArrayBody(t, className(t, objectClass(t, a)), 1)) {
        case 'B':
          return pushReference(t, makeByte(t, byteArrayBody(t, a, index)));
        case 'C':
          return pushReference(t, makeChar(t, charArrayBody(t, a, index)));
        case 'D':
          return pushReference(t, makeDouble(t, doubleArrayBody(t, a, index)));
        case 'F':
          return pushReference(t, makeFloat(t, floatArrayBody(t, a, index)));
        case 'I':
          return pushReference(t, makeInt(t, intArrayBody(t, a, index)));
        case 'J':
          return pushReference(t, makeLong(t, longArrayBody(t, a, index)));
        case 'S':
          return pushReference(t, makeShort(t, shortArrayBody(t, a, index)));
        case 'Z':
          return pushReference
            (t, makeBoolean(t, booleanArrayBody(t, a, index)));
        case 'L':
        case '[':
          return pushReference(t, objectArrayBody(t, a, index));

        default: abort(t);
        }
      } else {
        t->exception = makeArrayIndexOutOfBoundsException(t, 0);
      }
    } else {
      t->exception = makeIllegalArgumentException(t);
    }
  } else {
    t->exception = makeNullPointerException(t);
  }

  return 0;
}

void
Array_set(Thread* t, jobject array, int index, jobject value)
{
  if (LIKELY(array)) {
    object a = *array;
    object v = (value ? *value : 0);
    unsigned elementSize = classArrayElementSize(t, objectClass(t, a));

    if (LIKELY(elementSize)) {
      intptr_t length = cast<uintptr_t>(a, BytesPerWord);

      if (LIKELY(index >= 0 and index < length)) {
        switch (byteArrayBody(t, className(t, objectClass(t, a)), 1)) {
        case 'L':
        case '[':
          set(t, objectArrayBody(t, a, index), v);
          break;

        default: {
          uint8_t* p = &cast<uint8_t>
            (a, (2 * BytesPerWord) + (index * elementSize));
          if (v) {
            memcpy(p, &cast<uint8_t>(v, BytesPerWord), elementSize);
          } else {
            t->exception = makeNullPointerException(t);
          }
        } break;
        }
      } else {
        t->exception = makeArrayIndexOutOfBoundsException(t, 0);
      }
    } else {
      t->exception = makeIllegalArgumentException(t);
    }
  } else {
    t->exception = makeNullPointerException(t);
  }
}

jint
Array_getLength(Thread* t, jobject array)
{
  if (LIKELY(array)) {
    object a = *array;
    unsigned elementSize = classArrayElementSize(t, objectClass(t, a));

    if (LIKELY(elementSize)) {
      return cast<uintptr_t>(a, BytesPerWord);
    } else {
      t->exception = makeIllegalArgumentException(t);
    }
  } else {
    t->exception = makeNullPointerException(t);
  }
  return 0;
}

jobject
Array_makeObjectArray(Thread* t, jclass, jclass elementType, jint length)
{
  return pushReference(t, makeObjectArray(t, *elementType, length, true));
}

jobject
String_intern(Thread* t, jobject this_)
{
  return pushReference(t, intern(t, *this_));
}

void
System_arraycopy(Thread* t, jclass, jobject src, jint srcOffset, jobject dst,
                 jint dstOffset, jint length)
{
  if (LIKELY(src and dst)) {
    object s = *src;
    object d = *dst;

    if (LIKELY(objectClass(t, s) == objectClass(t, d))) {
      unsigned elementSize = classArrayElementSize(t, objectClass(t, s));

      if (LIKELY(elementSize)) {
        intptr_t sl = cast<uintptr_t>(s, BytesPerWord);
        intptr_t dl = cast<uintptr_t>(d, BytesPerWord);
        if (LIKELY(srcOffset >= 0 and srcOffset + length <= sl and
                   dstOffset >= 0 and dstOffset + length <= dl))
        {
          uint8_t* sbody = &cast<uint8_t>(s, 2 * BytesPerWord);
          uint8_t* dbody = &cast<uint8_t>(d, 2 * BytesPerWord);
          if (src == dst) {
            memmove(dbody + (dstOffset * elementSize),
                    sbody + (srcOffset * elementSize),
                    length * elementSize);
          } else {
            memcpy(dbody + (dstOffset * elementSize),
                   sbody + (srcOffset * elementSize),
                   length * elementSize);
          }
          return;
        }
      }
    }
  } else {
    t->exception = makeNullPointerException(t);
    return;
  }

  t->exception = makeArrayStoreException(t);
}

jint
System_identityHashCode(Thread* t, jclass, jobject o)
{
  if (LIKELY(o)) {
    return objectHash(t, *o);
  } else {
    t->exception = makeNullPointerException(t);
    return 0;
  }
}

void
Runtime_loadLibrary(Thread* t, jobject, jstring name)
{
  if (LIKELY(name)) {
    char n[stringLength(t, *name) + 1];
    stringChars(t, *name, n);

    for (System::Library* lib = t->vm->libraries; lib; lib = lib->next()) {
      if (::strcmp(lib->name(), n) == 0) {
        // already loaded
        return;
      }
    }

    System::Library* lib;
    if (LIKELY(t->vm->system->success
               (t->vm->system->load(&lib, n, t->vm->libraries))))
    {
      t->vm->libraries = lib;
    } else {
      object message = makeString(t, "library not found: %s", n);
      t->exception = makeRuntimeException(t, message);
    }
  } else {
    t->exception = makeNullPointerException(t);
  }
}

void
Runtime_gc(Thread* t, jobject)
{
  ENTER(t, Thread::ExclusiveState);

  collect(t, Heap::MajorCollection);
}

void
Runtime_exit(Thread* t, jobject, jint code)
{
  t->vm->system->exit(code);
}

jlong
Runtime_freeMemory(Thread*, jobject)
{
  // todo
  return 0;
}

jobject
Throwable_trace(Thread* t, jclass, jint skipCount)
{
  int frame = t->frame;
  while (skipCount-- and frame >= 0) {
    frame = frameNext(t, frame);
  }
  
  if (methodClass(t, frameMethod(t, frame))
      == arrayBody(t, t->vm->types, Machine::ThrowableType))
  {
    // skip Throwable constructors
    while (strcmp(reinterpret_cast<const int8_t*>("<init>"),
                  &byteArrayBody(t, methodName(t, frameMethod(t, frame)), 0))
           == 0)
    {
      frame = frameNext(t, frame);
    }
  }

  return pushReference(t, makeTrace(t, frame));
}

jarray
Throwable_resolveTrace(Thread* t, jclass, jobject trace)
{
  unsigned length = arrayLength(t, *trace);
  object array = makeObjectArray
    (t, arrayBody(t, t->vm->types, Machine::StackTraceElementType),
     length, true);
  PROTECT(t, array);

  object e = 0;
  PROTECT(t, e);

  object class_ = 0;
  PROTECT(t, class_);

  for (unsigned i = 0; i < length; ++i) {
    e = arrayBody(t, *trace, i);

    class_ = className(t, methodClass(t, traceElementMethod(t, e)));
    class_ = makeString(t, class_, 0, byteArrayLength(t, class_) - 1, 0);

    object method = methodName(t, traceElementMethod(t, e));
    method = makeString(t, method, 0, byteArrayLength(t, method) - 1, 0);

    unsigned line = lineNumber
      (t, traceElementMethod(t, e), traceElementIp(t, e));

    object ste = makeStackTraceElement(t, class_, method, 0, line);
    set(t, objectArrayBody(t, array, i), ste);
  }

  return pushReference(t, array);
}

jobject
Thread_currentThread(Thread* t, jclass)
{
  return pushReference(t, t->javaThread);
}

jlong
Thread_doStart(Thread* t, jobject this_)
{
  Thread* p = new (t->vm->system->allocate(sizeof(Thread)))
    Thread(t->vm, *this_, t);

  enter(p, Thread::ActiveState);

  if (t->vm->system->success(t->vm->system->start(&(p->runnable)))) {
    return reinterpret_cast<jlong>(p);
  } else {
    p->exit();
    return 0;
  }
}

void
Thread_interrupt(Thread* t, jclass, jlong peer)
{
  interrupt(t, reinterpret_cast<Thread*>(peer));
}

jlong
ResourceInputStream_open(Thread* t, jclass, jstring path)
{
  if (LIKELY(path)) {
    char p[stringLength(t, *path) + 1];
    stringChars(t, *path, p);

    return reinterpret_cast<jlong>(t->vm->finder->find(p));
  } else {
    t->exception = makeNullPointerException(t);
    return 0;
  }
}

jint
ResourceInputStream_read(Thread*, jclass, jlong peer, jint position)
{
  Finder::Data* d = reinterpret_cast<Finder::Data*>(peer);
  if (position >= static_cast<jint>(d->length())) {
    return -1;
  } else {
    return d->start()[position];
  }
}

jint
ResourceInputStream_read2(Thread* t, jclass, jlong peer, jint position,
                          jbyteArray b, jint offset, jint length)
{
  Finder::Data* d = reinterpret_cast<Finder::Data*>(peer);
  if (length > static_cast<jint>(d->length()) - position) {
    length = static_cast<jint>(d->length()) - position;
  }
  if (length < 0) {
    return -1;
  } else {
    memcpy(&byteArrayBody(t, *b, offset), d->start() + position, length);
    return length;
  }
}

void
ResourceInputStream_close(Thread*, jclass, jlong peer)
{
  reinterpret_cast<Finder::Data*>(peer)->dispose();
}

} // namespace

namespace vm {

void
populateBuiltinMap(Thread* t, object map)
{
  struct {
    const char* key;
    void* value;
  } builtins[] = {
    { "Java_java_lang_Class_isAssignableFrom",
      reinterpret_cast<void*>(::Class_isAssignableFrom) },
    { "Java_java_lang_Class_primitiveClass",
      reinterpret_cast<void*>(::Class_primitiveClass) },
    { "Java_java_lang_Class_initialize",
      reinterpret_cast<void*>(::Class_initialize) },

    { "Java_java_lang_ClassLoader_defineClass",
      reinterpret_cast<void*>(::ClassLoader_defineClass) },

    { "Java_java_lang_System_arraycopy",
      reinterpret_cast<void*>(::System_arraycopy) },

    { "Java_java_lang_SystemClassLoader_findClass",
      reinterpret_cast<void*>(::SystemClassLoader_findClass) },
    { "Java_java_lang_SystemClassLoader_findLoadedClass",
      reinterpret_cast<void*>(::SystemClassLoader_findLoadedClass) },
    { "Java_java_lang_SystemClassLoader_resourceExists",
      reinterpret_cast<void*>(::SystemClassLoader_resourceExists) },

    { "Java_java_lang_Runtime_loadLibrary",
      reinterpret_cast<void*>(::Runtime_loadLibrary) },
    { "Java_java_lang_Runtime_gc",
      reinterpret_cast<void*>(::Runtime_gc) },
    { "Java_java_lang_Runtiime_exit",
      reinterpret_cast<void*>(::Runtime_exit) },

    { "Java_java_lang_String_intern",
      reinterpret_cast<void*>(::String_intern) },

    { "Java_java_lang_Thread_doStart",
      reinterpret_cast<void*>(::Thread_doStart) },
    { "Java_java_lang_Thread_interrupt",
      reinterpret_cast<void*>(::Thread_interrupt) },
    { "Java_java_lang_Thread_currentThread",
      reinterpret_cast<void*>(::Thread_currentThread) },

    { "Java_java_lang_Throwable_resolveTrace",
      reinterpret_cast<void*>(::Throwable_resolveTrace) },
    { "Java_java_lang_Throwable_trace",
      reinterpret_cast<void*>(::Throwable_trace) },

    { "Java_java_lang_Object_getClass",
      reinterpret_cast<void*>(::Object_getClass) },
    { "Java_java_lang_Object_notify",
      reinterpret_cast<void*>(::Object_notify) },
    { "Java_java_lang_Object_notifyAll",
      reinterpret_cast<void*>(::Object_notifyAll) },
    { "Java_java_lang_Object_toString",
      reinterpret_cast<void*>(::Object_toString) },
    { "Java_java_lang_Object_wait",
      reinterpret_cast<void*>(::Object_wait) },
    { "Java_java_lang_Object_hashCode",
      reinterpret_cast<void*>(::Object_hashCode) },

    { "Java_java_lang_reflect_Array_get",
      reinterpret_cast<void*>(::Array_get) },
    { "Java_java_lang_reflect_Array_set",
      reinterpret_cast<void*>(::Array_set) },
    { "Java_java_lang_reflect_Array_getLength",
      reinterpret_cast<void*>(::Array_getLength) },
    { "Java_java_lang_reflect_Array_makeObjectArray",
      reinterpret_cast<void*>(::Array_makeObjectArray) },

    { "Java_java_lang_reflect_Constructor_make",
      reinterpret_cast<void*>(::Constructor_make) },

    { "Java_java_lang_reflect_Field_get",
      reinterpret_cast<void*>(::Field_get) },
    { "Java_java_lang_reflect_Field_set",
      reinterpret_cast<void*>(::Field_set) },

    { "Java_java_lang_reflect_Method_getCaller",
      reinterpret_cast<void*>(::Method_getCaller) },
    { "Java_java_lang_reflect_Method_invoke",
      reinterpret_cast<void*>(::Method_invoke) },

    { "Java_java_net_URL_00024ResourceInputStream_open",
      reinterpret_cast<void*>(::ResourceInputStream_open) },
    { "Java_java_net_URL_00024ResourceInputStream_read_JI",
      reinterpret_cast<void*>(::ResourceInputStream_read) },
    { "Java_java_net_URL_00024ResourceInputStream_read_JI_3BII",
      reinterpret_cast<void*>(::ResourceInputStream_read2) },
    { "Java_java_net_URL_00024ResourceInputStream_close",
      reinterpret_cast<void*>(::ResourceInputStream_close) },

    { "Java_java_io_ObjectInputStream_makeInstance",
      reinterpret_cast<void*>(::ObjectInputStream_makeInstance) },

    { 0, 0 }
  };

  for (unsigned i = 0; builtins[i].key; ++i) {
    object key = makeByteArray(t, builtins[i].key);
    PROTECT(t, key);
    object value = makePointer(t, builtins[i].value);

    hashMapInsert(t, map, key, value, byteArrayHash);
  }
}

} // namespace vm
