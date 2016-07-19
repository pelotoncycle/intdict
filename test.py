from unittest import TestCase

from intdict import intdict

class TestIntdict(TestCase):
    def test(self):
        d = intdict()
        d[1] = "one"
        d[2] = "two"
        d[-2] = "minus two"
        self.assertNotIn(0, d)
        self.assertNotIn(3, d)
        self.assertIn(2, d)
        self.assertEquals(d[1], "one")
        with self.assertRaises(TypeError):
            d[-1] = "minus one"
        self.assertEquals(d[2], "two")
        self.assertEquals(d[-2], "minus two")
        with self.assertRaises(KeyError):
            d[3]
        with self.assertRaises(KeyError):
            d[-1]
            
        self.assertIn(1, d)
        self.assertIn(2, d)
        self.assertIn(-2, d)
        self.assertNotIn(-1, d)
        self.assertNotIn(3, d)
    
