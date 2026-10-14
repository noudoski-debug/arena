import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('offsets', Path(__file__).parents[1] / 'tools/offsets.py')
o = importlib.util.module_from_spec(spec)
spec.loader.exec_module(o)

class OffsetTests(unittest.TestCase):
    def test_namespace_and_overload(self):
        text = '''// Namespace: Wrong
public class Camera : Object
{
// RVA: 0x999 Offset: 0x111 VA: 0x999
public int Value() { }
}
// Namespace: UnityEngine
public sealed class Camera : Behaviour
{
// RVA: 0x5000 Offset: 0x1000 VA: 0x5000
public int Value() { }
// RVA: 0x6000 Offset: 0x2000 VA: 0x6000
public int Value(int x) { }
}
'''
        block = o.class_block(text, 'UnityEngine', 'Camera')
        self.assertEqual(o.method_rva(block, 'public int Value()'), 0x5000)
        self.assertEqual(o.method_rva(block, 'public int Value(int x)'), 0x6000)
        with self.assertRaises(ValueError):
            o.class_block(text+text, 'UnityEngine', 'Camera')

    def test_struct_namespace(self):
        text = """// Namespace: Other
public struct RaycastHit
{
public int other; // 0x0
}
// Namespace: UnityEngine
public struct RaycastHit
{
internal int m_Collider; // 0x28
}
"""
        block = o.class_block(text, 'UnityEngine', 'RaycastHit')
        self.assertEqual(o.field_offset(block, 'm_Collider'), 0x28)

    def test_missing_or_generic_rva(self):
        for text in ('public int Value() { }', '// RVA: -1 Offset: -1\npublic int Value() { }'):
            with self.assertRaises(ValueError): o.method_rva(text, 'public int Value()')
        with self.assertRaises(ValueError):
            o.method_rva('// RVA: 0x100 Offset: 0x80\npublic void Other() { }\npublic int Value() { }', 'public int Value()')

    def test_field_is_not_method_offset(self):
        self.assertEqual(o.field_offset('public float hp; // 0x88\n// Methods\n// hp; // 0x99', 'hp'), 0x88)
        with self.assertRaises(ValueError): o.field_offset('public float other; // 0x88', 'hp')

    def test_zip_utf16_and_ambiguous_dump(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'dump.zip'
            with zipfile.ZipFile(path, 'w') as z: z.writestr('sub/dump.cs', 'hello'.encode('utf-16'))
            self.assertEqual(o.read_dump(path), 'hello')
            with zipfile.ZipFile(path, 'a') as z: z.writestr('dump.cs', b'other')
            with self.assertRaises(ValueError): o.read_dump(path)

if __name__ == '__main__': unittest.main()
