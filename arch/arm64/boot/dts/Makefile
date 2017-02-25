dts-dirs += apm
dts-dirs += arm
dts-dirs += cavium
ifeq ($(CONFIG_MACH_LGE),y)
dts-dirs += lge
else
dts-dirs += qcom
endif

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
