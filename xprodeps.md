# hdf5 dependencies

|project|license [^_l]|description [dependencies]|version|source|diff [^_d]|
|-------|-------------|--------------------------|-------|------|----------|
|<a id='hdf5' />[hdf5](https://www.hdfgroup.org/solutions/hdf5/)|[BSD-3-Clause](https://github.com/HDFGroup/hdf5/blob/develop/LICENSE 'BSD 3-Clause New or Revised License')|Utilize the HDF5 high performance data software library and file format to manage, process, and store your heterogeneous data. HDF5 is built for fast I/O processing and storage. [deps: _zlib_]| |[upstream](https://github.com/HDFGroup/hdf5 'github.com/HDFGroup/hdf5')|  [patch]|
|<a id='zlib' />[zlib](https://zlib.net 'zlib website')|[permissive](https://zlib.net/zlib_license.html 'zlib/libpng license, see https://en.wikipedia.org/wiki/Zlib_License')|compression library|[xpv1.3.1.4](https://github.com/externpro/zlib/releases/tag/xpv1.3.1.4 'release')|[repo](https://github.com/externpro/zlib 'github.com/externpro/zlib') [upstream](https://github.com/madler/zlib 'github.com/madler/zlib')|[diff](https://github.com/externpro/zlib/compare/v1.3.1...xpv1.3.1.4 'github.com/externpro/zlib/compare/v1.3.1...xpv1.3.1.4') [patch]|

![deps](xprodeps.svg 'dependencies')

Dependency version check: all 1 parent-manifest versions match pinned versions.

|diff  |description|
|------|-----------|
|patch |diff modifies/patches existing cmake|
|intro |diff introduces cmake|
|auto  |diff adds cmake to replace autotools/configure/make|
|native|diff adds cmake but uses existing build system|
|bin   |diff adds cmake to repackage binaries built elsewhere|
|fetch |diff adds cmake and utilizes FetchContent|

[^_l]: see [SPDX License List](https://spdx.org/licenses/ '') for a list of commonly found licenses
[^_d]: see table above with description of diff
