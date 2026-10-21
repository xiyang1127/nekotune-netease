# 网易云音乐插件

`nekotune.netease` 将 [zaixiayesheng/netease-music-api](https://github.com/zaixiayesheng/netease-music-api) 的网易云请求逻辑移植到 NekoTune 插件：以 C++ 原生扩展完成 weapi 加密，直接访问网易云。**无需部署 API 服务、运行 PHP 或填写服务地址。**

## 使用

1. 在「扩展」导入独立构建的安装包，或挂载 `build/extensions/nekotune.netease/`。主程序不再随附或自动登记此插件。
2. 启用并信任插件，即可从侧栏「网易云音乐」搜索歌曲，或输入歌单 ID、歌曲 ID 浏览。
3. 单击歌曲行选中，双击或 Enter 播放；「入队」只追加队列，「下载」只下载入库，不自动切歌。
4. 「设置 → 扩展设置 → 网易云设置」可选 128 / 192 / 320 kbps、优先逐字歌词、是否显示翻译。默认 320 kbps，并开启逐字歌词和翻译；网易云没有逐字数据时自动回退 LRC。

插件安装后按宿主规则默认停用；首次启用需要信任。启用即注册音乐和歌词来源，无额外服务开关。停用撤销来源并取消请求，不删除用户的音乐或队列。升级时自动移除早期适配器遗留的服务地址配置。

## 移植内容

- **weapi**：AES-128-CBC 双层加密、每次请求随机 16 字节密钥、RSA 模幂计算 `encSecKey`。AES 与 1024 位模幂在插件内实现（`weapi.cpp`），不依赖 PHP、OpenSSL 或其他库，并用上游测试向量校验。
- **搜索**：直连 `/weapi/cloudsearch/pc`，关键词最多 64 字，每页 30 首，使用上游总数判断翻页。
- **歌曲详情**：`/weapi/v3/song/detail/`；保留长整数 ID，转换歌曲、歌手、专辑、时长和封面。
- **歌单**：`/weapi/v6/playlist/detail` 读取曲目 ID，再按 500 首一批补全详情；保持歌单顺序。最多 4 个并发详情请求、10,000 首、25 秒总时限。
- **音频**：`/weapi/song/enhance/player/url` 传递所选码率，解析 `url` / `uf.url`，交给宿主通用播放、下载和取消流程。临时地址及静态客户端 Cookie 不写入歌曲元数据。
- **歌词**：`/weapi/song/lyric` 获取 LRC、YRC 和翻译。YRC 的绝对逐字时间转换为宿主 KRC 的相对时间；翻译按时间戳合并到原文同一行，不是独立翻译轨。歌词入口不改变本地歌词优先级。
- **封面**：优先使用网易云返回的封面；缺失时使用原项目 XOR + MD5 的图片 ID 算法。只接受网易云 CDN 的 HTTPS 地址，加载失败显示默认封面。
- **缓存**：歌单 10 分钟，歌词 7 天；插件进程内缓存，上限 32 项 / 8 MiB，重启或停用后清空。相同请求合并，失败不缓存。

源码来源与移植差异见 [SOURCE.md](SOURCE.md)。插件除 Qt 外没有第三方依赖，不内置登录会话，不提供账号登录，也不绕过会员、地区或版权限制。可用音质和音频由网易云实际响应决定。下载音频不自动写入歌词或封面侧载文件。

## 开发与打包

在本项目根目录执行 `cmake -S . -B build && cmake --build build`，生成可挂载的 `build/extensions/nekotune.netease/` 和安装包 `build/packages/nekotune.netease-<平台>.nekotune.zip`，不会放入宿主源码或发行目录。工具依赖和测试步骤见 [项目说明](../README.md)。修改源码后重新构建；挂载的开发目录会自动重载。

音乐来源 ID 为 `nekotune.netease/music`，歌词来源为 `nekotune.netease/lyrics`。专属音乐页不重复显示在通用音源入口中。以下服务通过 `extensions.call` 调用，`id` 为 `nekotune.netease`：

| 服务 | 参数与结果 |
| --- | --- |
| `status` | 返回 `{bitrate,word_lyrics,translation}` |
| `config.set` | 接收上述三项设置，成功后返回保存的值 |
| `search` | `{query,cursor?}`；页码字符串从 1 开始，返回 `{tracks,cursor?}` |
| `song` | `{id}`；返回 `{tracks}`，一条曲目 |
| `playlist` | `{id}`；返回 `{tracks}`，不自动创建本地歌单 |
| `lyric` | `{id,dwrc?,trlrc?}`；返回原始 `{lyric,tlyric,type}`，无 YRC 时回退 LRC |

配置事件为 `extension.nekotune.netease.status`，载荷位于 `data`。播放、入队和下载使用宿主 `music.enqueue`、`music.download`、`music.cancel`。后台 JSON 响应上限 4 MiB、单次请求超时 20 秒，音频下载上限 100 MiB；禁止跟随 API 重定向到第三方地址。

自动回归使用加密测试向量和模拟网易云响应，覆盖分批歌单、长整数、歌词时间、缓存、异常和停用取消，不访问真实账号。
