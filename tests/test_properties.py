# coding=utf-8

import sys
import struct
import unittest

from gi.repository import GObject
from  gi.repository.GObject import GType, GEnum, new, PARAM_READWRITE, \
     PARAM_CONSTRUCT, PARAM_READABLE, PARAM_WRITABLE, PARAM_CONSTRUCT_ONLY
from gi.repository.GObject import \
     TYPE_INT, TYPE_UINT, TYPE_LONG, \
     TYPE_ULONG, TYPE_INT64, TYPE_UINT64
from gi.repository.GObject import \
     G_MININT, G_MAXINT, G_MAXUINT, G_MINLONG, G_MAXLONG, \
     G_MAXULONG

from gi.repository import Gio
from gi.repository import GLib

if sys.version_info < (3, 0):
    TEST_UTF8 = "\xe2\x99\xa5"
    UNICODE_UTF8 = unicode(TEST_UTF8, 'UTF-8')
else:
    TEST_UTF8 = "♥"
    UNICODE_UTF8 = TEST_UTF8

from compathelper import _long

class PropertyObject(GObject.GObject):
    normal = GObject.Property(type=str)
    construct = GObject.Property(
        type=str,
        flags=PARAM_READWRITE|PARAM_CONSTRUCT, default='default')
    construct_only = GObject.Property(
        type=str,
        flags=PARAM_READWRITE|PARAM_CONSTRUCT_ONLY)
    uint64 = GObject.Property(
        type=TYPE_UINT64, flags=PARAM_READWRITE|PARAM_CONSTRUCT)

    enum = GObject.Property(
        type=Gio.SocketType, default=Gio.SocketType.STREAM)

    boxed = GObject.Property(
        type=GLib.Regex, flags=PARAM_READWRITE|PARAM_CONSTRUCT)

class TestProperties(unittest.TestCase):
    def testGetSet(self):
        obj = PropertyObject()
        obj.props.normal = "value"
        self.assertEqual(obj.props.normal, "value")

    def testListWithInstance(self):
        obj = PropertyObject()
        self.failUnless(hasattr(obj.props, "normal"))

    def testListWithoutInstance(self):
        self.failUnless(hasattr(PropertyObject.props, "normal"))

    def testSetNoInstance(self):
        def set(obj):
            obj.props.normal = "foobar"

        self.assertRaises(TypeError, set, PropertyObject)

    def testIterator(self):
        for obj in (PropertyObject.props, PropertyObject().props):
            for pspec in obj:
                gtype = GType(pspec)
                self.assertEqual(gtype.parent.name, 'GParam')
                self.failUnless(pspec.name in ['normal',
                                               'construct',
                                               'construct-only',
                                               'uint64',
                                               'enum',
                                               'boxed'])
            self.assertEqual(len(obj), 6)

    def testNormal(self):
        obj = new(PropertyObject, normal="123")
        self.assertEqual(obj.props.normal, "123")
        obj.set_property('normal', '456')
        self.assertEqual(obj.props.normal, "456")
        obj.props.normal = '789'
        self.assertEqual(obj.props.normal, "789")

    def testConstruct(self):
        obj = new(PropertyObject, construct="123")
        self.assertEqual(obj.props.construct, "123")
        obj.set_property('construct', '456')
        self.assertEqual(obj.props.construct, "456")
        obj.props.construct = '789'
        self.assertEqual(obj.props.construct, "789")

    def testUTF8(self):
        obj = new(PropertyObject, construct_only=UNICODE_UTF8)
        self.assertEqual(obj.props.construct_only, TEST_UTF8)
        obj.set_property('construct', UNICODE_UTF8)
        self.assertEqual(obj.props.construct, TEST_UTF8)
        obj.props.normal = UNICODE_UTF8
        self.assertEqual(obj.props.normal, TEST_UTF8)

    def testIntToStr(self):
        obj = new(PropertyObject, construct_only=1)
        self.assertEqual(obj.props.construct_only, '1')
        obj.set_property('construct', '2')
        self.assertEqual(obj.props.construct, '2')
        obj.props.normal = 3
        self.assertEqual(obj.props.normal, '3')

    def testConstructOnly(self):
        obj = new(PropertyObject, construct_only="123")
        self.assertEqual(obj.props.construct_only, "123")
        self.assertRaises(TypeError,
                          setattr, obj.props, 'construct_only', '456')
        self.assertRaises(TypeError,
                          obj.set_property, 'construct-only', '456')

    def testUint64(self):
        obj = new(PropertyObject)
        self.assertEqual(obj.props.uint64, 0)
        obj.props.uint64 = _long(1)
        self.assertEqual(obj.props.uint64, _long(1))
        obj.props.uint64 = 1
        self.assertEqual(obj.props.uint64, _long(1))

        self.assertRaises((TypeError, OverflowError), obj.set_property, "uint64", _long(-1))
        self.assertRaises((TypeError, OverflowError), obj.set_property, "uint64", -1)

    def testUInt64DefaultValue(self):
        try:
            class TimeControl(GObject.GObject):
                __gproperties__ = {
                    'time': (TYPE_UINT64, 'Time', 'Time',
                             _long(0), (1<<64) - 1, _long(0),
                             PARAM_READABLE)
                    }
        except OverflowError:
            (etype, ex) = sys.exc_info()[2:]
            self.fail(str(ex))

    def testEnum(self):
        obj = new(PropertyObject)
        self.assertEqual(obj.props.enum, Gio.SocketType.STREAM)
        self.assertEqual(obj.enum, Gio.SocketType.STREAM)
        obj.enum = Gio.SocketType.DATAGRAM
        self.assertEqual(obj.props.enum, Gio.SocketType.DATAGRAM)
        self.assertEqual(obj.enum, Gio.SocketType.DATAGRAM)
        obj.props.enum = Gio.SocketType.STREAM
        self.assertEqual(obj.props.enum, Gio.SocketType.STREAM)
        self.assertEqual(obj.enum, Gio.SocketType.STREAM)
        obj.props.enum = 2
        self.assertEqual(obj.props.enum, Gio.SocketType.DATAGRAM)
        self.assertEqual(obj.enum, Gio.SocketType.DATAGRAM)
        obj.enum = 1
        self.assertEqual(obj.props.enum, Gio.SocketType.STREAM)
        self.assertEqual(obj.enum, Gio.SocketType.STREAM)

        self.assertRaises(TypeError, setattr, obj, 'enum', 'foo')
        self.assertRaises(TypeError, setattr, obj, 'enum', object())

        self.assertRaises(TypeError, GObject.Property, type=Gio.SocketType)
        self.assertRaises(TypeError, GObject.Property, type=Gio.SocketType,
                          default=Gio.SocketProtocol.TCP)
        self.assertRaises(TypeError, GObject.Property, type=Gio.SocketType,
                          default=object())
        self.assertRaises(TypeError, GObject.Property, type=Gio.SocketType,
                          default=1)

    def textBoxed(self):
        obj = new(PropertyObject)

        regex = GLib.Regex.new('[a-z]*', 0, 0)
        obj.props.boxed = regex
        self.assertEqual(obj.props.boxed.get_pattern(), '[a-z]*')
        self.assertEqual(obj.boxed.get_patttern(), '[a-z]*')

        self.assertRaises(TypeError, setattr, obj, 'boxed', 'foo')
        self.assertRaises(TypeError, setattr, obj, 'boxed', object())

    def testRange(self):
        # kiwi code
        def max(c):
            return 2 ** ((8 * struct.calcsize(c)) - 1) - 1
        def umax(c):
            return 2 ** (8 * struct.calcsize(c)) - 1

        maxint = max('i')
        minint = -maxint - 1
        maxuint = umax('I')
        maxlong = max('l')
        minlong = -maxlong - 1
        maxulong = umax('L')
        maxint64 = max('q')
        minint64 = -maxint64 - 1
        maxuint64 = umax('Q')

        types = dict(int=(TYPE_INT, minint, maxint),
                     uint=(TYPE_UINT, 0, maxuint),
                     long=(TYPE_LONG, minlong, maxlong),
                     ulong=(TYPE_ULONG, 0, maxulong),
                     int64=(TYPE_INT64, minint64, maxint64),
                     uint64=(TYPE_UINT64, 0, maxuint64))

        def build_gproperties(types):
            d = {}
            for key, (gtype, min, max) in types.items():
                d[key] = (gtype, 'blurb', 'desc', min, max, 0,
                          PARAM_READABLE | PARAM_WRITABLE)
            return d

        class RangeCheck(GObject.GObject):
            __gproperties__ = build_gproperties(types)

            def __init__(self):
                self.values = {}
                GObject.GObject.__init__(self)

            def do_set_property(self, pspec, value):
                self.values[pspec.name] = value

            def do_get_property(self, pspec):
                return self.values.get(pspec.name, pspec.default_value)

        self.assertEqual(RangeCheck.props.int.minimum, minint)
        self.assertEqual(RangeCheck.props.int.maximum, maxint)
        self.assertEqual(RangeCheck.props.uint.minimum, 0)
        self.assertEqual(RangeCheck.props.uint.maximum, maxuint)
        self.assertEqual(RangeCheck.props.long.minimum, minlong)
        self.assertEqual(RangeCheck.props.long.maximum, maxlong)
        self.assertEqual(RangeCheck.props.ulong.minimum, 0)
        self.assertEqual(RangeCheck.props.ulong.maximum, maxulong)
        self.assertEqual(RangeCheck.props.int64.minimum, minint64)
        self.assertEqual(RangeCheck.props.int64.maximum, maxint64)
        self.assertEqual(RangeCheck.props.uint64.minimum, 0)
        self.assertEqual(RangeCheck.props.uint64.maximum, maxuint64)

        obj = RangeCheck()
        for key, (gtype, min, max) in types.items():
            self.assertEqual(obj.get_property(key),
                             getattr(RangeCheck.props, key).default_value)

            obj.set_property(key, min)
            self.assertEqual(obj.get_property(key), min)

            obj.set_property(key, max)
            self.assertEqual(obj.get_property(key), max)


    def testMulti(self):
        obj = PropertyObject()
        obj.set_properties(normal="foo",
                           uint64=7)
        normal, uint64 = obj.get_properties("normal", "uint64")
        self.assertEqual(normal, "foo")
        self.assertEqual(uint64, 7)

class TestProperty(unittest.TestCase):
    def testSimple(self):
        class C(GObject.GObject):
            str = GObject.Property(type=str)
            int = GObject.Property(type=int)
            float = GObject.Property(type=float)
            long = GObject.Property(type=_long)

        self.failUnless(hasattr(C.props, 'str'))
        self.failUnless(hasattr(C.props, 'int'))
        self.failUnless(hasattr(C.props, 'float'))
        self.failUnless(hasattr(C.props, 'long'))

        o = C()
        self.assertEqual(o.str, '')
        o.str = 'str'
        self.assertEqual(o.str, 'str')

        self.assertEqual(o.int, 0)
        o.int = 1138
        self.assertEqual(o.int, 1138)

        self.assertEqual(o.float, 0.0)
        o.float = 3.14
        self.assertEqual(o.float, 3.14)

        self.assertEqual(o.long, _long(0))
        o.long = _long(100)
        self.assertEqual(o.long, _long(100))

    def testCustomGetter(self):
        class C(GObject.GObject):
            def get_prop(self):
                return 'value'
            prop = GObject.Property(getter=get_prop)

        o = C()
        self.assertEqual(o.prop, 'value')
        self.assertRaises(TypeError, setattr, o, 'prop', 'xxx')

    def testCustomSetter(self):
        class C(GObject.GObject):
            def set_prop(self, value):
                self._value = value
            prop = GObject.Property(setter=set_prop)

            def __init__(self):
                self._value = None
                GObject.GObject.__init__(self)

        o = C()
        self.assertEquals(o._value, None)
        o.prop = 'bar'
        self.assertEquals(o._value, 'bar')
        self.assertRaises(TypeError, getattr, o, 'prop')

    def testDecoratorDefault(self):
        class C(GObject.GObject):
            _value = 'value'
            @GObject.Property
            def value(self):
                return self._value
            @value.setter
            def value(self, value):
                self._value = value

        o = C()
        self.assertEqual(o.value, 'value')
        o.value = 'blah'
        self.assertEqual(o.value, 'blah')
        self.assertEqual(o.props.value, 'blah')

    def testDecoratorWithCall(self):
        class C(GObject.GObject):
            _value = 1
            @GObject.Property(type=int, default=1, minimum=1, maximum=10)
            def typedValue(self):
                return self._value
            @typedValue.setter
            def typedValue(self, value):
                self._value = value

        o = C()
        self.assertEqual(o.typedValue, 1)
        o.typedValue = 5
        self.assertEqual(o.typedValue, 5)
        self.assertEqual(o.props.typedValue, 5)

    def testErrors(self):
        self.assertRaises(TypeError, GObject.Property, type='str')
        self.assertRaises(TypeError, GObject.Property, nick=False)
        self.assertRaises(TypeError, GObject.Property, blurb=False)
        # this never fail while bool is a subclass of int
        # >>> bool.__bases__
        # (<type 'int'>,)
        # self.assertRaises(TypeError, GObject.Property, type=bool, default=0)
        self.assertRaises(TypeError, GObject.Property, type=bool, default='ciao mamma')
        self.assertRaises(TypeError, GObject.Property, type=bool)
        self.assertRaises(TypeError, GObject.Property, type=object, default=0)
        self.assertRaises(TypeError, GObject.Property, type=complex)
        self.assertRaises(TypeError, GObject.Property, flags=-10)

    def testDefaults(self):
        p1 = GObject.Property(type=bool, default=True)
        p2 = GObject.Property(type=bool, default=False)

    def testNameWithUnderscore(self):
        class C(GObject.GObject):
            prop_name = GObject.Property(type=int)
        o = C()
        o.prop_name = 10
        self.assertEqual(o.prop_name, 10)

    def testRange(self):
        maxint64 = 2 ** 62 - 1
        minint64 = -2 ** 62 - 1
        maxuint64 = 2 ** 63 - 1

        types = [
            (TYPE_INT, G_MININT, G_MAXINT),
            (TYPE_UINT, 0, G_MAXUINT),
            (TYPE_LONG, G_MINLONG, G_MAXLONG),
            (TYPE_ULONG, 0, G_MAXULONG),
            (TYPE_INT64, minint64, maxint64),
            (TYPE_UINT64, 0, maxuint64),
            ]

        for gtype, min, max in types:
            # Normal, everything is alright
            prop = GObject.Property(type=gtype, minimum=min, maximum=max)
            subtype = type('', (GObject.GObject,),
                         dict(prop=prop))
            self.assertEqual(subtype.props.prop.minimum, min)
            self.assertEqual(subtype.props.prop.maximum, max)

            # Lower than minimum
            self.assertRaises(TypeError,
                              GObject.Property, type=gtype, minimum=min-1,
                              maximum=max)

            # Higher than maximum
            self.assertRaises(TypeError,
                              GObject.Property, type=gtype, minimum=min,
                              maximum=max+1)

    def testMinMax(self):
        class C(GObject.GObject):
            prop_int = GObject.Property(type=int, minimum=1, maximum=100, default=1)
            prop_float = GObject.Property(type=float, minimum=0.1, maximum=10.5, default=1.1)

            def __init__(self):
                GObject.GObject.__init__(self)

        o = C()
        self.assertEqual(o.prop_int, 1)

        o.prop_int = 5
        self.assertEqual(o.prop_int, 5)

        o.prop_int = 0
        self.assertEqual(o.prop_int, 5)

        o.prop_int = 101
        self.assertEqual(o.prop_int, 5)

        self.assertEqual(o.prop_float, 1.1)

        o.prop_float = 7.75
        self.assertEqual(o.prop_float, 7.75)

        o.prop_float = 0.09
        self.assertEqual(o.prop_float, 7.75)

        o.prop_float = 10.51
        self.assertEqual(o.prop_float, 7.75)

    def testMultipleInstances(self):
        class C(GObject.GObject):
            prop = GObject.Property(type=str, default='default')

        o1 = C()
        o2 = C()
        self.assertEqual(o1.prop, 'default')
        self.assertEqual(o2.prop, 'default')
        o1.prop = 'value'
        self.assertEqual(o1.prop, 'value')
        self.assertEqual(o2.prop, 'default')

    def testObjectProperty(self):
        class PropertyObject(GObject.GObject):
            obj = GObject.Property(type=GObject.GObject)

        pobj1 = PropertyObject()
        obj1_hash = hash(pobj1)
        pobj2 = PropertyObject()

        pobj2.obj = pobj1
        del pobj1
        pobj1 = pobj2.obj
        self.assertEqual(hash(pobj1), obj1_hash)

    def testObjectSubclassProperty(self):
        class ObjectSubclass(GObject.GObject):
            __gtype_name__ = 'ObjectSubclass'

        class PropertyObjectSubclass(GObject.GObject):
            obj = GObject.Property(type=ObjectSubclass)

        obj1 = PropertyObjectSubclass(obj=ObjectSubclass())

    def testPropertySubclass(self):
        # test for #470718
        class A(GObject.GObject):
            prop1 = GObject.Property(type=int)

        class B(A):
            prop2 = GObject.Property(type=int)

        b = B()
        b.prop2 = 10
        self.assertEquals(b.prop2, 10)
        b.prop1 = 20
        self.assertEquals(b.prop1, 20)

    def testPropertySubclassCustomSetter(self):
        # test for #523352
        class A(GObject.GObject):
            def get_first(self):
                return 'first'
            first = GObject.Property(type=str, getter=get_first)

        class B(A):
            def get_second(self):
                return 'second'
            second = GObject.Property(type=str, getter=get_second)

        a = A()
        self.assertEquals(a.first, 'first')
        self.assertRaises(TypeError, setattr, a, 'first', 'foo')

        b = B()
        self.assertEquals(b.first, 'first')
        self.assertRaises(TypeError, setattr, b, 'first', 'foo')
        self.assertEquals(b.second, 'second')
        self.assertRaises(TypeError, setattr, b, 'second', 'foo')

    def testPropertySubclassCustomSetterError(self):
        try:
            class A(GObject.GObject):
                def get_first(self):
                    return 'first'
                first = GObject.Property(type=str, getter=get_first)

                def do_get_property(self, pspec):
                    pass
        except TypeError:
            pass
        else:
            raise AssertionError

    # Bug 587637.
    def test_float_min(self):
        GObject.Property(type=float, minimum=-1)
        GObject.Property(type=GObject.TYPE_FLOAT, minimum=-1)
        GObject.Property(type=GObject.TYPE_DOUBLE, minimum=-1)

    # Bug 644039
    def testReferenceCount(self):
        # We can check directly if an object gets finalized, so we will
        # observe it indirectly through the refcount of a member object.

        # We create our dummy object and store its current refcount
        o = object()
        rc = sys.getrefcount(o)

        # We add our object as a member to our newly created object we
        # want to observe. Its refcount is increased by one.
        t = PropertyObject(normal="test")
        t.o = o
        self.assertEquals(sys.getrefcount(o), rc + 1)

        # Now we want to ensure we do not leak any references to our
        # object with properties. If no ref is leaked, then when deleting
        # the local reference to this object, its reference count shoud
        # drop to zero, and our dummy object should loose one reference.
        del t
        self.assertEquals(sys.getrefcount(o), rc)

    def testDocStringAsBlurb(self):
        class C(GObject.GObject):
            @GObject.Property
            def blurbed(self):
                """blurbed doc string"""
                return 0

        self.assertEqual(C.blurbed.blurb, 'blurbed doc string')


if __name__ == '__main__':
    unittest.main()
