# 历法数据来源与许可

产品使用的农历及节气数值由 SXTWL 生成，来源为
https://github.com/yuangu/sxtwl_cpp ，固定提交
`7598b0601a76cfdaa9266257b1b5690720c1e2ce`，许可证见
[LICENSE.sxtwl.txt](LICENSE.sxtwl.txt)。

C 解码器的公历序号转换和压缩农历月设计参考
https://github.com/isee15/Lunar-Solar-Calendar-Converter ，固定提交
`55d4e547947594736f7c9e4a35521ecc1c7342d8`，许可证见
[LICENSE.isee15.txt](LICENSE.isee15.txt)。未使用该项目原有日期表作为生产输入。

固件编译使用同级上层目录中已生成的 `product_ui_calendar_data.inc`
及 `product_ui_calendar_lunar.c` 解码器。生成与测试工具在开发工作区维护，
普通公开工程的编译、烧录及 OTA 打包不运行这些工具。
许可证和来源说明应随相应源码及分发材料保留。
