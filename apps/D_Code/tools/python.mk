# Resolve the same concrete interpreter used by explicit D_PYTHON builds.
# Keep clean/help usable without Python; never run a firmware/post-build target here.
D_PYTHON_BUILD_GOALS := $(filter-out clean help,$(MAKECMDGOALS))$(if $(MAKECMDGOALS),,default)
ifneq ($(strip $(D_PYTHON_BUILD_GOALS)),)
ifeq ($(OS),Windows_NT)
D_PYTHON_NULL := NUL
else
D_PYTHON_NULL := /dev/null
endif

# -I excludes user startup hooks and -B avoids generating cache files.
# Print a quoted absolute path so installation directories may contain spaces.
D_PYTHON_PROBE := -I -B -c "import sys; assert sys.version_info >= (3,10); print(chr(34)+sys.executable.replace(chr(92),chr(47))+chr(34))"
ifeq ($(origin D_PYTHON),undefined)
ifeq ($(OS),Windows_NT)
D_PYTHON := $(strip $(shell py -3 $(D_PYTHON_PROBE) 2>$(D_PYTHON_NULL)))
endif
ifeq ($(strip $(D_PYTHON)),)
D_PYTHON := $(strip $(shell python3 $(D_PYTHON_PROBE) 2>$(D_PYTHON_NULL)))
endif
ifeq ($(strip $(D_PYTHON)),)
D_PYTHON := $(strip $(shell python $(D_PYTHON_PROBE) 2>$(D_PYTHON_NULL)))
endif
endif

ifeq ($(strip $(D_PYTHON)),)
$(error Python 3.10+ is required. Install Python or set D_PYTHON to its quoted executable path)
endif
D_PYTHON_READY := $(strip $(shell $(D_PYTHON) -I -B -c "import sys; assert sys.version_info >= (3,10); print('ready')" 2>$(D_PYTHON_NULL)))
ifneq ($(D_PYTHON_READY),ready)
$(error D_PYTHON cannot start Python 3.10+. Check its path and quote paths containing spaces)
endif
endif
