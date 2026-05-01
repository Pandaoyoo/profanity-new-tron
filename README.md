# TRX地址、TRX20-USDT靓号生成器使用说明

## 修复**profanity**（CVE-2022-40769）漏洞，利用**GPU**加速、代码全开源并去除后门盗U暗桩

## 1. 命令

```powershell
.\tron_vanity.exe -f tron.txt -t 1 -w 1 -q
```
- 从当前**tron.txt**读取目标地址，自动运行识别当前电脑是否存在GPU，生成目标前1位和后1位相同的地址。（用于地址钓鱼）。

```powershell
.\tron_vanity.exe -f tron.txt -w 4 -q
```
- 从当前**tron.txt**读取目标地址，生成目标后4位相同的地址（用于靓号生成3A、4A、5A、6A、7A均可-速度取决于显卡）。

## 2. 参数含义

- `-f tron.txt`：从**tron.txt**读取目标 TRON 地址（每行一个地址）。
- `-t 1`：按目标地址“前 1 位”做匹配。
- `-w 1`：按目标地址“后 1 位”做匹配。
- `-q`：找到一条满足条件的结果后自动退出。

说明：以上组合表示“地址首尾各匹配 1 位后停止”。

## 3.`tron.txt`格式

- 纯文本文件，每行一个 TRON 地址。
- 建议地址以**T**开头，长度约 34 位。

示例：

```text
TLQmUHVpqPHsikvGEULL1msCHZyQV8E888
```

## 4. 运行前准备
- 电脑自带GPU显卡或者购买云服务商的GPU服务。
- 将**tron_vanity.exe**、**tron.txt**放在同一目录（或使用完整路径）。
- 需要可用的 OpenCL 设备（通常是显卡）和驱动。对应的显卡驱动，https://www.nvidia.cn/Download/index.aspx  

- 举例腾讯云 V100 显卡搜索下载选择：
Product Type：	Data Center / Tesla
Product Series：	A-Series
Product:	NVIDIA A100
Operating System: 就是你的windows 如：Windows Server 2019。

## 5. 结果输出

- 命中时终端会打印：耗时、分数、私钥、地址。
- 同时追加写入当前目录下的**trx.txt**。

## 6. 常见问题

- 提示**no devices found**：未检测到可用 OpenCL 设备，请检查显卡驱动/OpenCL 运行环境。
- 提示无法打开文件：确认**tron.txt**路径正确，且文件有可读权限。

## 7. 安全注意事项

- ⚠️ **私钥安全**: 生成的私钥非常重要，请妥善保管。
- 💾 **备份**: 定期备份找到的靓号地址。
- 🚫 **不要分享**: 不要分享自己的任何私钥信息、钱包不要点击任何二维码、链接，避免资产被授权盗取。
