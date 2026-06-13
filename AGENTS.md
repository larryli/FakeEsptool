烧录器端参考代码库：

1、目录 `esptool` 对应 https://github.com/espressif/esptool 代码库

2、目录 `esptool-js` 对应 https://github.com/espressif/esptool-js 代码库

3、目录 `web-esptool` 对应 https://github.com/xingrz/web-esptool 代码库

协议解码器参考代码库：

1、目录 `sigrok-esp32-programmer-decoder` 对应 https://github.com/ruediste/sigrok-esp32-programmer-decoder 代码库

文档说明：

1、`DEVELOPMENT.md` 开发文档除了开发环境的说明，重点是具体实现的细节暴露，特别是针对协议文档未明事项的补充说明。用于总结开发结果。

2、`PROTOCOL.md` 协议文档只是整理公开的 esptool 协议内容，是对官方文档的分析和烧录器代码实现的逆向总结。用于开发参考。

3、`REQUIREMENTS.md` 需求文档是功能需求说明，着重是程序功能的描述，而非实现细节。用于指导开发。

4、`TODO.md` 待办文档是记录问题、计划开发。用于管理开发。已完成的项目清理具体项目不要再保留在待办中，然后同步到其他文档（需求、开发、协议文档）。空的 TODO 文档也需要保留。

特别注意：

1、请先配置好开发环境 MS Build Tools + CMake，了解烧录器端、协议解码器代码和本项目文档。

2、请使用 VS Installer 工具 vswhere.exe 定位 MS Build Tools 安装位置。

3、代码修改完成后自动构建验证修改。修改无误，结束对话前自动运行最后构建的程序。仅修改文档和注释不需要自动构建。

4、除了 esptool 官方代码实现，其他代码不一定准确。

5、代码和文档文件请保持 Windows 换行符 CR+LF 保存，并且文件结尾必须有换行符。
