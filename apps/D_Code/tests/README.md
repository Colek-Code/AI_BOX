# 固件主机回归

在工程根目录执行。需要 Python 3 和带 pthread 的 MinGW GCC；编译器路径可通过 `--cc` 指定。

```powershell
python -B -X utf8 apps/D_Code/tests/run_runtime_tests.py --cc D:/mingw64/bin/gcc.exe --work-dir apps/D_Code/build/out/runtime-regression
```

- 闹钟：11 类读取结果的双槽组合、旧记录夹具、默认预设、保存失败和恢复，以及两个负控。测试调用生产服务、模型和 GC9307 存储适配，VM 由文件替代。
- BLE：输出长度、缓冲容量、身份字段、格式化失败、加密失败和三个负控。提取并编译实际生产函数，蓝牙发送由有界替身替代。
- Qwen：检查公开库与头文件的摘要、目标及 ABI 绑定，并验证损坏绑定会被拒绝。主机测试不执行 pi32 库，不替代 WSS 实机会话验收。

`contracts/` 保存独立行为预期。`host/` 只用于主机测试，不进入固件源码清单。生成的程序、模拟 VM、日志和结果统一写入指定 `build/out/` 目录；这些测试不会烧录、擦除或连接设备。
