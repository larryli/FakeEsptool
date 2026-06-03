开发环境配置：

1、系统安装有 Visual Studio 2026 Build Tools，在 "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\"

2、系统配置 WSL Ubuntu 环境，内置 Python 支持

烧录器端参考代码库：

1、相对项目根目录 ../esptool/esptool-js/ 对应 https://github.com/espressif/esptool-js 代码库

2、相对项目根目录 ../esptool/web-esptool/ 对应 https://github.com/xingrz/web-esptool 代码库

协议解码器参考代码库：

1、相对项目根目录 ../esptool/sigrok-esp32-programmer-decoder/ 对应 https://github.com/ruediste/sigrok-esp32-programmer-decoder 代码库


注意：

1、请先配置好开发环境，了解烧录器端、协议解码器代码和本项目文档。

2、通过 vcvarsall.bat 设置完整环境后使用 CMake 构建。

3、代码修改完成后自动构建验证修改。修改无误，结束对话前自动运行最后构建的程序。

4、除了 esptool-js 官方烧录器代码实现，其他代码不一定准确。
