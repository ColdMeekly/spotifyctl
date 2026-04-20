"""
Custom build hook so wheels get a platform-specific tag (py3-none-win_amd64)
even though no C extension is compiled by setuptools. The DLL is bundled via
package_data; CI copies it into libspotifyctl/_prebuilt/ before `pip wheel`.
"""

from setuptools import setup

try:
    from wheel.bdist_wheel import bdist_wheel as _bdist_wheel
except ImportError:
    _bdist_wheel = None


if _bdist_wheel is not None:

    class bdist_wheel(_bdist_wheel):
        def finalize_options(self):
            super().finalize_options()
            # Force a platform wheel — we ship a .dll, not Python bytecode alone.
            self.root_is_pure = False

        def get_tag(self):
            _python, _abi, plat = super().get_tag()
            return "py3", "none", plat

    setup(cmdclass={"bdist_wheel": bdist_wheel})
else:
    setup()
