# 来源与移植说明

协议和业务流程参考：

- 项目：[zaixiayesheng/netease-music-api](https://github.com/zaixiayesheng/netease-music-api)
- 核对版本：[`6f90867d9dff71a08641fa1345adbb666542846f`](https://github.com/zaixiayesheng/netease-music-api/tree/6f90867d9dff71a08641fa1345adbb666542846f)
- `api/music/v1/_shared.php`：weapi、公钥参数、请求头、封面 ID 算法与歌曲字段。
- `api/music/v1/search.php`、`song.php`、`playlist.php`、`url.php`、`lyric.php`：上游路由、请求参数、歌曲批量查询与歌词回退。
- 上游 `_shared.php` 标明其加密实现源于 metowolf/Meting v1.5.11。

本插件用原生 C++ 实现协议，不运行原 PHP 网关。HTTP 路由、CORS、Web 服务器限流和 `/health` 属于服务部署层，不随插件移植。

主要适配：

1. 每层 AES 只做一次 PKCS#7 填充，不再叠加 PHP helper 的手动填充。RSA 模幂由插件内的大整数实现完成，每次随机生成密钥，无固定密钥降级。
2. 不伪造 `X-Real-IP`；保留兼容所需的静态客户端 UA、Referer 和非登录 Cookie。TLS 证书验证保持开启。
3. 不输出 HTTP 302；解析音频后直接交给宿主。网络错误、响应体和日志不暴露临时音频地址。
4. 大整数 ID 使用原始 JSON 十进制文本，避免超过 JavaScript 安全整数范围后改变封面或歌曲身份。
5. 文件缓存变为有界进程内缓存，避免另建服务目录；只缓存成功的歌单和歌词。歌单请求受宿主 RPC 时限约束。
6. YRC 转为宿主已支持的 KRC 文本，普通歌词与翻译按时间戳合并；无需修改宿主播放器或歌词存储格式。
