try:
    from setuptools import setup, Extension
except ImportError:
    from distutils.core import setup, Extension

setup(name='intdict',
      author = 'Adam DePrince',
      author_email = 'adam@pelotoncycle.com',
      url = 'https://github.com/pelotoncycle/peloton_bloomfilters',
      version='0.0.1',
      description="Peloton Cycle's insanely fast integer dictionary",
      ext_modules=(
          [
              Extension(
                  name='intdict',
                  sources=['intdictmodule.c']),
          ]
      )
)
