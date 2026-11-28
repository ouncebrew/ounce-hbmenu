#---------------------------------------------------------------------------------
# nx-hbmenu for Switch 2, built with the Switch 2 devkit (libounce and OunceVK).
#
# This is upstream's Makefile.nx with these changes:
# - It includes libounce's switch2_rules in place of libnx's switch_rules.
# - Graphics: switch2/ draws with Vulkan (-lvulkan) in place of deko3d. It
#   replaces nx_main/nx_graphics.c. The GLSL shaders in switch2/shaders are
#   compiled to SPIR-V headers with glslangValidator.
# - The romfs's assets.zip replaces the built-in icons' bitmaps with their
#   SVG files (switch2/pack_romfs.py). The NRO's icon is switch2/icon.jpg,
#   made from switch2/vectors/app_icon.svg (make icon, with ImageMagick).
# - The romfs, the icon and the output directory are variables.
#
# Run it from the repository root, with the devkit installed (or build the
# Dockerfile, which runs it in the devkit's image):
#   make -f switch2/Makefile    ->  build/nro/nro/hbmenu.nro
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libounce/switch2_rules

APP_VERSION	:=	1337.69421.l33t
APP_TITLE	:=	hbmenu
APP_AUTHOR	:=	switchbrew, ImDella, Miella Selene / Xayah

TARGET		:=	hbmenu
BUILD		?=	build/sdk
OUTDIR		?=	build/nro/nro
SOURCES		:=	common/ nx_main/ nx_main/loaders/ switch2/
SHADERS		:=	switch2/shaders
DATA		:=	data
INCLUDES	:=	include
ROMFS		?=	build/nro/romfs
ICON		?=	switch2/icon.jpg

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	$(SWITCH2_CPU) -mtp=soft -fPIE

PKGS	:=	libconfig libturbojpeg freetype2 physfs

CFLAGS	:=	-g -Wall -O2 -ffunction-sections -fdata-sections \
			$(ARCH) $(DEFINES) `$(PREFIX)pkg-config --cflags $(PKGS)`

CFLAGS	+=	$(INCLUDE) -D__SWITCH__ -DVERSION=\"v$(APP_VERSION)\"

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(LIBNX)/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lvulkan `$(PREFIX)pkg-config --libs $(PKGS)` -lkaihen -lnx

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX)

ifneq ($(notdir $(BUILD)),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(OUTDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(SHADERS),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

# switch2/graphics_vulkan.c replaces the deko3d graphics.
CFILES		:=	$(filter-out nx_graphics.c,$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c))))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
GLSLFILES	:=	$(foreach dir,$(SHADERS),$(notdir $(wildcard $(dir)/*.vert $(dir)/*.frag)))

export LD	:=	$(CC)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))
# quad.vert -> hbmenu_quad_vert.h
export HFILES_SPV	:=	$(foreach f,$(GLSLFILES),hbmenu_$(subst .,_,$(f)).h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON	:=	$(TOPDIR)/$(ICON)
export NROFLAGS	+=	--icon=$(APP_ICON) --nacp=$(OUTPUT).nacp
ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all icon

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD): $(ROMFS)/assets.zip
	@[ -d $@ ] || mkdir -p $@
	@mkdir -p $(OUTDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/switch2/Makefile

$(ROMFS)/assets.zip: $(wildcard assets/*) $(wildcard switch2/vectors/*.svg) switch2/pack_romfs.py
	@echo $(notdir $@)
	@python3 switch2/pack_romfs.py $@

# The NRO's icon, a 256x256 JPEG, from the SVG (ImageMagick with librsvg).
icon:
	magick -background white -density 96 switch2/vectors/app_icon.svg \
		-resize '256x256!' -flatten -quality 92 switch2/icon.jpg

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(ROMFS) $(OUTPUT).nro $(OUTPUT).nacp $(OUTPUT).elf

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all	:	$(OUTPUT).nro

$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN) $(HFILES_SPV)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

# The SPIR-V of a shader, as a C array named like the header.
hbmenu_quad_%.h : quad.%
	@echo $(notdir $<)
	@glslangValidator -V --quiet --vn $(basename $@) -o $@ $<

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
