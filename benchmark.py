from intdict import intdict
import sys
import psutil

try:
    capacity = int(sys.argv[3])

    if sys.argv[1] == 'intdict':
        d = intdict()
    elif sys.argv[1] == 'dict':
        d = dict()
    elif sys.argv[1] == 'intdict_prealloc':
        d = intdict(int(capacity))
    elif sys.argv[1] == 'null':
        d = None
    else:
        raise ValueError()

    if sys.argv[2] == 'sequential':
        randomize = False
    elif sys.argv[2] == 'random':
        randomize = True
        from random import randint
    else:
        raise ValueError()
        
except:
    sys.stderr.write('python benchmark.py null|dict|intdict|intdict_prealloc sequential|random capacity\n')
    sys.exit(1) 


import time
t = time.time()
randint_capacity = capacity * capacity 
if d == None:
    if randomize:
        for x in xrange(capacity):
            x = randint(0, randint_capacity)
            d = str(x)
    else:
        for x in xrange(capacity):
            d = str(x)
else:
    if randomize:
        for x in xrange(capacity):
            x = randint(0, randint_capacity)
            d[x] = str(x)

    else:
        for x in xrange(capacity):
            d[x] = str(x)

print sys.argv[1], capacity, time.time() - t, psutil.Process().memory_info().rss

