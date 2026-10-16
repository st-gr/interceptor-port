# The environment of the Playwright engines in the OpenShell sandbox (no root, no apt): source it before tools/web/smoke.py.
#   . tools/web/pw-env.sh
# /sandbox/pwlibs holds the shared libraries the browsers need (tools/web/README.md lists how it was made); /sandbox/pwlibs-wk holds two older
# sonames only WebKit wants (libxml2.so.2, libjxl.so.0.8), searched last. Do not run the engines under `ulimit -v` (Chromium dies with SIGTRAP).
export PLAYWRIGHT_BROWSERS_PATH=/sandbox/.pw
export LD_LIBRARY_PATH=/sandbox/pwlibs/lib:/sandbox/pwlibs-wk/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
# Playwright's own check wants the GTK variant's libraries (gtk4, vulkan, x264 ...); headless WebKit is the WPE build, which does not
export PLAYWRIGHT_SKIP_VALIDATE_HOST_REQUIREMENTS=1
# WPE needs an EGL display: Mesa's software rasteriser, no GPU
export __EGL_VENDOR_LIBRARY_DIRS=/sandbox/pwlibs/share/glvnd/egl_vendor.d
export LIBGL_DRIVERS_PATH=/sandbox/pwlibs/lib/dri
export LIBGL_ALWAYS_SOFTWARE=1
# Firefox: no user namespaces here, so no content sandbox
export MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_GMP_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1 MOZ_DISABLE_RDD_SANDBOX=1 MOZ_DISABLE_SOCKET_PROCESS_SANDBOX=1 MOZ_DISABLE_UTILITY_SANDBOX=1
