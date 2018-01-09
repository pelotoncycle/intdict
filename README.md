# intdict

`intdict` is a high performance integer -> PyObject data structure
intended for mapping 64 bit integers such as what might be returned by
`hash` or `id` to a Python object.


## Installation

`pip install intdict`

##  Usage

```
>>> from intdict import intdict
>>> d = intdict()
>>> d[1] = 'one'
>>> d[2] = 'two'
>>> d[1]
'one'
>>> d[3]
KeyError
```

You may also preallocate `intdict` objects to a specific size for even
better performance.

```
>>> d = intdict(10000)
```

## Caveats

`intdict` stores all signed 64 bit integers except for the value of
`-1`.  This happens to also be exactly the range of the `hash` function
on a 64 bit machine and a superset of the range of values `id` can
return.

## Benchmarks

If you can live within intdict's mild restrictions, here are the
performance numbers you can enjoy.  These timings were taken from an
`Intel(R) Core(TM) i7-4790 CPU @ 3.60GHz` running Ubuntu 15.04.


Storing 1,000,000 sequential numbers

| Dict type  | Seconds | Memory  |
| ---------- | ------- | --------|
| dict       | 15.3    | 1,153Mb |
| int dict   | 1.3     | 773Mb   |
| prealloced | 0.3     | 773Mb   |

Storing 1,000,000 random numbers

| Dict type  | Seconds | Memory  |
| ---------- | ------- | ------- |
| dict       | 3       | 1,234Mb |
| intdict    | 1.5     | 854Mb   |
| prealloced | 1.3     | 854Mb   |
