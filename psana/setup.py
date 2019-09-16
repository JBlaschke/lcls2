
import os
import sys
import numpy as np
from setuptools import setup, Extension, find_packages

## HIDE WARNING:
## cc1plus: warning: command line option "-Wstrict-prototypes" is valid for C/ObjC but not for C++
from distutils.sysconfig import get_config_vars
cfg_vars = get_config_vars()
for k, v in cfg_vars.items():
    if type(v) == str:
        cfg_vars[k] = v.replace("-Wstrict-prototypes", "")


print('Begin: %s' % ' '.join(sys.argv))

arg = [arg for arg in sys.argv if arg.startswith('--instdir')]
if not arg:
    raise Exception('Parameter --instdir is missing')
instdir = arg[0].split('=')[1]
sys.argv.remove(arg[0])


if sys.platform == 'darwin':
    extra_compile_args = ['-std=c++11', '-mmacosx-version-min=10.9']
    extra_link_args = ['-mmacosx-version-min=10.9']
else:
    #extra_compile_args=['-std=c++11']
    # Flag -Wno-cpp hides warning: 
    #warning "Using deprecated NumPy API, disable it with " "#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION" [-Wcpp]
    extra_compile_args=['-std=c++11', '-Wno-cpp']
    extra_link_args = []

extra_link_args_rpath = extra_link_args + ['-Wl,-rpath,'+ os.path.abspath(os.path.join(instdir, 'lib'))]


dgram_module = Extension('psana.dgram',
                         sources = ['src/dgram.cc'],
                         libraries = ['xtc','shmemcli'],
                         include_dirs = ['src', np.get_include(), os.path.join(instdir, 'include')],
                         library_dirs = [os.path.join(instdir, 'lib')],
                         extra_link_args = extra_link_args_rpath,
                         extra_compile_args = extra_compile_args)

seq_module = Extension('psana.seq',
                         sources = ['src/seq.cc'],
                         libraries = ['xtc'],
                         include_dirs = [np.get_include(), os.path.join(instdir, 'include')],
                         library_dirs = [os.path.join(instdir, 'lib')],
                         extra_link_args = extra_link_args_rpath,
                         extra_compile_args = extra_compile_args)

container_module = Extension('psana.container',
                         sources = ['src/container.cc'],
                         libraries = ['xtc'],
                         include_dirs = [np.get_include(), os.path.join(instdir, 'include')],
                         library_dirs = [os.path.join(instdir, 'lib')],
                         extra_link_args = extra_link_args_rpath,
                         extra_compile_args = extra_compile_args)

       #cmdclass = {'build': build_ext, 'build_ext': my_build_ext},
       #cmdclass={'build_ext': my_build_ext},
setup(
       name = 'psana',
       license = 'LCLS II',
       description = 'LCLS II analysis package',
       install_requires=[
         'numpy',
       ],
       packages = find_packages(),
       include_package_data = True,
       package_data={'graphqt': ['data/icons/*.png','data/icons/*.gif'],
       },
       #cmdclass={'build_ext': my_build_ext},
       ext_modules = [dgram_module, seq_module, container_module],
       entry_points={
            'console_scripts': [
                'convert_npy_to_txt  = psana.pyalgos.app.convert_npy_to_txt:do_main',
                'convert_txt_to_npy  = psana.pyalgos.app.convert_txt_to_npy:do_main',
                'merge_mask_ndarrays = psana.pyalgos.app.merge_mask_ndarrays:do_main',
                'merge_max_ndarrays  = psana.pyalgos.app.merge_max_ndarrays:do_main',
                'cdb                 = psana.pscalib.app.cdb:cdb_cli',
                'proc_info           = psana.pscalib.app.proc_info:do_main',
                'proc_control        = psana.pscalib.app.proc_control:do_main',
                'proc_new_datasets   = psana.pscalib.app.proc_new_datasets:do_main',
                'timeconverter       = psana.graphqt.app.timeconverter:timeconverter',
                'calibman            = psana.graphqt.app.calibman:calibman_gui',
                'detnames            = psana.app.detnames:detnames',
             ]
       },
)

CYT_BLD_DIR = 'build'

from Cython.Build import cythonize
ext = Extension('shmem',
                sources=["psana/shmem/shmem.pyx"],
                libraries = ['xtc','shmemcli'],
                include_dirs = [np.get_include(), os.path.join(instdir, 'include')],
                library_dirs = [os.path.join(instdir, 'lib')],
                language="c++",
                extra_compile_args = extra_compile_args,
                extra_link_args = extra_link_args_rpath,
)

setup(name="shmem",
      ext_modules=cythonize(ext, build_dir=CYT_BLD_DIR))


ext = Extension("peakFinder",
                sources=["psana/peakFinder/peakFinder.pyx",
                         "../psalg/psalg/peaks/src/PeakFinderAlgos.cc",
                         "../psalg/psalg/peaks/src/LocalExtrema.cc"],
                language="c++",
                extra_compile_args = extra_compile_args,
                extra_link_args = extra_link_args,
                include_dirs=[np.get_include(), os.path.join(instdir, 'include')],
)

setup(name="peakFinder",
      ext_modules=cythonize(ext, build_dir=CYT_BLD_DIR))


                         #"../psalg/psalg/hexanode/src/hexanode.cc",
                         #"../psalg/psalg/hexanode/src/wrap_resort64c.cc",
                         #     os.path.join(instdir, 'include'),
                         #"../psalg/psalg/hexanode/src/LMF_IO.cc",

if(os.path.isfile(os.path.join(sys.prefix, 'lib', 'libResort64c_x64.a'))):
    ext = Extension("hexanode",
                    sources=["psana/hexanode/hexanode_ext.pyx",
                             "../psalg/psalg/hexanode/src/cfib.cc"],
                    libraries=['psalg',],
                    language="c++",
                    extra_compile_args = extra_compile_args,
                    include_dirs=[np.get_include(), os.path.join(instdir, 'include'), os.path.join(sys.prefix,'include'),],
                    library_dirs = [os.path.join(instdir, 'lib')],
                    extra_link_args = extra_link_args,
                    )
    setup(name="hexanode",
          ext_modules=cythonize(ext, build_dir=CYT_BLD_DIR))


#                libraries=['psalg',],

# ugly: only build hexanode apps if the roentdek software exists.
# this is a rough python equivalent of the way cmake finds out whether
# packages exist. - cpo
if False :
  if(os.path.isfile(os.path.join(sys.prefix, 'lib', 'libResort64c_x64.a'))):
    ext = Extension("hexanode",
                    sources=["psana/hexanode/test_ext.pyx",
                             "../psalg/psalg/hexanode/src/cfib.cc"],
                    language="c++",
                    extra_compile_args = extra_compile_args,
                    include_dirs=[np.get_include(), os.path.join(instdir, 'include')],
                    library_dirs = [os.path.join(instdir, 'lib')],
                    extra_link_args = extra_link_args,
                )
    setup(name="hexanode",
          ext_modules=cythonize(ext, build_dir=CYT_BLD_DIR))





ext = Extension("constFracDiscrim",
                sources=["psana/constFracDiscrim/constFracDiscrim.pyx",
                         "../psalg/psalg/constFracDiscrim/src/ConstFracDiscrim.cc"],
                language="c++",
                extra_compile_args = extra_compile_args,
                include_dirs=[os.path.join(sys.prefix,'include'), np.get_include(), os.path.join(instdir, 'include')],
)

setup(name="constFracDiscrim",
      ext_modules=cythonize(ext, build_dir=CYT_BLD_DIR))

ext = Extension('dgramCreate',
                #packages=['psana.peakfinder',],
                sources=["psana/peakFinder/dgramCreate.pyx"],
                libraries = ['xtc'],
                include_dirs = [np.get_include(), os.path.join(instdir, 'include')],
                library_dirs = [os.path.join(instdir, 'lib')],
                language="c++",
                extra_compile_args = extra_compile_args,
                extra_link_args = extra_link_args_rpath,
                # include_dirs=[np.get_include(),
                              # "../install/include"]
)

setup(name='dgramCreate',
      ext_modules=cythonize(ext, build_dir=CYT_BLD_DIR))

setup(name='dgramchunk',
      ext_modules = cythonize(Extension(
                    "psana.dgramchunk",
                    sources=["src/dgramchunk.pyx"],
      ), build_dir=CYT_BLD_DIR))

setup(name='smdreader',
      ext_modules = cythonize(Extension(
                    "psana.smdreader",
                    sources=["psana/smdreader.pyx"],
                    include_dirs=["psana"],
      ), build_dir=CYT_BLD_DIR))

setup(name='eventbuilder', 
      ext_modules = cythonize(Extension(
                    "psana.eventbuilder",                                 
                    sources=["psana/eventbuilder.pyx"],  
                    include_dirs=["psana"],
      ), build_dir=CYT_BLD_DIR))

setup(name='parallelreader',
      ext_modules = cythonize(Extension(
                    "psana.parallelreader",
                    sources=["psana/parallelreader.pyx"],
                    include_dirs=["psana"],
                    #extra_compile_args=['-fopenmp'], # This picks up system compiler and could potentially be
                    #extra_link_args=['-fopenmp'],    # bad when sys. comp. is linked with extra stuffs.
                    libraries = ['gomp'],             # Instead, tells compiler to use libgomp from conda directly.
                    library_dirs = [os.path.join(os.environ['CONDA_PREFIX'],'lib')],
      ), build_dir=CYT_BLD_DIR))

ext = Extension("hsd",
                sources=["psana/hsd/hsd.pyx",
                         "../psalg/psalg/peaks/src/PeakFinderAlgos.cc",
                         "../psalg/psalg/peaks/src/LocalExtrema.cc"],
                libraries=['xtc','psalg','digitizer'],
                language="c++",
                extra_compile_args=extra_compile_args,
                include_dirs=[np.get_include(),
                              "../install/include",
                              os.path.join(instdir, 'include')],
                library_dirs = [os.path.join(instdir, 'lib')],
                extra_link_args = extra_link_args_rpath,
)

setup(name="hsd",
      ext_modules=cythonize(ext, build_dir=CYT_BLD_DIR),
)


