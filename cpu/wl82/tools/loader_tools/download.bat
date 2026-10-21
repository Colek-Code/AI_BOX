set OBJDUMP=C:\JL\pi32\bin\llvm-objdump.exe
set OBJCOPY=C:\JL\pi32\bin\llvm-objcopy.exe
set ELFFILE=sdk.elf
REM set OUTPUT_IMAGE=-output-sdcrad-image jl_sdcard.img
REM set KEY_FILE=-key JL_791N-XXXX.key
REM set KEY_FILE=-key1 JL_791N-XXXX.key1 -mkey JL_791N-XXXX.mkey
%OBJCOPY% -O binary -j .text %ELFFILE% text.bin
%OBJCOPY% -O binary -j .data %ELFFILE% data.bin
%OBJCOPY% -O binary -j .ram0_data %ELFFILE% ram0_data.bin
%OBJCOPY% -O binary -j .cache_ram_data %ELFFILE% cache_ram_data.bin
%OBJDUMP% -section-headers -address-mask=0x1ffffff %ELFFILE%
%OBJDUMP% -t %ELFFILE% > symbol_tbl.txt
copy /b text.bin+data.bin+ram0_data.bin+cache_ram_data.bin app.bin
isd_download.exe isd_config.ini -gen2 -tonorflash -dev wl82 -boot 0x1c02000 -div1 -wait 300 -uboot uboot.boot -app app.bin cfg_tool.bin -reboot 500 %KEY_FILE% -update_files normal -extend-bin %OUTPUT_IMAGE%
fw_add.exe -noenc -fw jl_isd.fw -add ota.bin -type 100 -out jl_isd.fw
fw_add.exe -noenc -fw jl_isd.fw -add script.ver -out jl_isd.fw
ufw_maker.exe -fw_to_ufw jl_isd.fw
copy jl_isd.ufw update.ufw
ping /n 2 127.1>null
del cache_ram_data.bin
del data.bin
del ram0_data.bin
del text.bin
exit /b 0
