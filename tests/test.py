import sys, os
sys.path.insert(0, os.getcwd())

import unittest
import fileevents as fe

WATCH_PATH = os.path.abspath('.')

def callback(path, flags, ctx):
    print "callback", path, ', ', flags, ', ', ctx

"""
class TestBadArgs(unittest.TestCase):
    
    def test_init_args(self):
        with self.assertRaises(ValueError):
            fe.init(None)
            
        with self.assertRaises(ValueError):
            fe.init(1)
            
    def test_close_args(self):
        with self.assertRaises(ValueError):
            fe.close(None)
        
        with self.assertRaises(ValueError):
            fe.close(1)
    
    def test_init_close(self):
        fw = fe.init(callback)
        sys.stdout.flush()
        
        fe.close(fw)

"""
class Test(unittest.TestCase):
    
    def setUp(self):
        self.fw = fe.init(callback)
        
    def tearDown(self):
        fe.close(self.fw)
        self.fw = None
    
    def _test_bad_args(self):
        pass
        #with self.assertRaises(ValueError):
        #    fe.add_watch(None, WATCH_PATH, fe.FE_ALL)
        
        #with self.assertRaises(ValueError):
        #    fe.add_watch(fw, WATCH_PATH, fe.FE_ALL)
            
        #with self.assertRaises(ValueError):
        #    fe.add_watch(fw, 'no exist', fe.FE_ALL)
    
    def test_simple_watch(self):
        #fe.add_watch(self.fw, os.path.abspath('.') + '\\', fe.FE_ALL )
        fe.add_watch(self.fw, r'c:\tmp', fe.FE_ALL )
        
        with open(r'c:\tmp\foo.txt', 'wb') as f:
            f.write('test')
            
        pass


if __name__ == '__main__':
    unittest.main()