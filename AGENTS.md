烧录器端参考代码库：

1、相对项目根目录 ../esptool/esptool-js/ 对应 https://github.com/espressif/esptool-js 代码库

2、相对项目根目录 ../esptool/web-esptool/ 对应 https://github.com/xingrz/web-esptool 代码库

协议解码器参考代码库：

1、相对项目根目录 ../esptool/sigrok-esp32-programmer-decoder/ 对应 https://github.com/ruediste/sigrok-esp32-programmer-decoder 代码库


注意：

1、请先配置好开发环境，了解烧录器端、协议解码器代码和本项目文档。

2、代码修改完成后自动构建验证修改。修改无误，结束对话前自动运行最后构建的程序。

3、除了 esptool-js 官方烧录器代码实现，其他代码不一定准确。

构建环境：

本项目使用 MSVC + NMake 构建，需要通过 vcvarsall.bat 初始化环境。

由于 PowerShell 无法正确继承 vcvarsall.bat 设置的环境变量，必须创建临时批处理文件执行构建：

```bat
@echo off
cd /d %~dp0build
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
nmake
```

执行方式：将上述内容写入项目根目录的临时 .bat 文件，然后运行该批处理文件。

构建产物：build\FakeEsptool.exe
