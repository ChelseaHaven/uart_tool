# 模块分层与扩展约定

## 目录和职责

```text
main.cpp                         程序入口，仅装配服务和加载界面
src/
  core/
    bytesource.h                 原始字节数据源接口
    receivebuffer.*              256 KiB 原始字节历史及截断状态
    byteformatter.*              UTF-8 / HEX 只读格式化
  sources/serial/
    serialsource.*               QSerialPort 生命周期、参数校验、设备枚举和读取
  application/
    appsettings.*                用户偏好及 QSettings 持久化
    receivesession.*             与数据源无关的接收计数、暂停、50 ms 刷新及增量显示
    applicationservices.*        本程序装配入口，拥有对象并保证生命周期
  presentation/
    serialconnectionmodel.*      串口配置的 QML 属性与操作适配
qml/
  Main.qml                       窗口布局、全局状态栏和面板装配
  components/
    SerialConfigPanel.qml        连接参数和连接操作
    ReceivePanel.qml             接收文本、复制、滚动和显示操作
    SettingsPage.qml             独立设置页面及偏好开关
    AppButton.qml                公共按钮
    ConfigCombo.qml              公共下拉框
    FieldLabel.qml               公共标签
```

## 当前数据流

```text
设备 -> SerialSource -> ByteSource::bytesReceived
                           |
                           v
                     ReceiveSession
                     /            \
           ReceiveBuffer        ByteFormatter
                     \            /
                   displayUpdate 信号
                           |
                           v
                    ReceivePanel.qml
```

串口配置走独立的 `SerialConfigPanel -> SerialConnectionModel -> SerialSource` 路径。

- `SerialSource` 只处理设备；它不持有接收历史、显示模式、刷新定时器或 QML 属性。
- `ReceiveBuffer` 只保留原始字节，`ByteFormatter` 只负责转换表示，二者不依赖 SerialPort 或 Qt Quick。
- `ReceiveSession` 依赖 `ByteSource` 抽象，任意原始字节来源都可复用接收、显示和历史管理逻辑。
- `SerialConnectionModel` 只适配串口连接配置，不代理接收区操作。
- `Main.qml` 绑定 `serialConnection` 与 `receiveSession`；接收面板通过显式属性注入会话和连接状态。
- `ApplicationServices` 拥有 source、session 和连接模型。构造后再交给 QML；销毁顺序确保消费者先于 source 退出。

## 生命周期与接口约定

- 原始字节源派生 `ByteSource`，实现 `isOpen()` 和 `close()`；连接方式和参数由具体数据源定义。
- 成功开始新连接时发 `opened`，会话清空历史／计数并恢复显示；失败不能发 `opened`。
- 有数据时发 `bytesReceived`，每次信号只是读取到的一段数据，不能视为协议帧。
- 断开前交付已读取的剩余字节，再发 `closed`；会话刷新尚未发布的数据，断开后保留历史。
- 接收会话不负责打开设备、枚举端口或处理驱动错误；连接错误由串口模型暴露。
- 原始缓存和 UI 行为沿用上一版：256 KiB 缓存、最多 262144 个显示字符、50 ms 合并、暂停不停止接收。
- `uart_ui` 的 `LogDocumentSettings` 显式关闭底层 QTextDocument 的撤销历史。只读 TextEdit 仍会记录程序插入／删除，不能仅依靠原始缓存上限约束这部分内存。
- 当前各对象均在应用线程使用。以后来源放到工作线程时，使用 Qt 队列信号交付字节和生命周期，避免跨线程直接调用对象。

## 编译依赖与验证

CMake 拆成五个可复用库和一个程序：

| 目标 | 直接职责和依赖 |
| --- | --- |
| `receive_core` | 字节接口、缓存、格式化、接收会话；只链接 Qt Core |
| `serial_transport` | 串口接入；链接 receive_core、Qt SerialPort |
| `protocol_core` | 通用协议、结构编译、文件模板与解析会话；只链接 Qt Core |
| `uart_application` | 本程序的串口配置适配及装配；链接以上三个库 |
| `uart_ui` | 文本显示的文档设置；链接 Qt Quick，不进入接收和协议核心 |
| `appuart_tool` | QML 桌面程序；链接应用层、Qt Quick 与 QuickControls2 |

`receive_core_tests` 使用内存字节源和 QCoreApplication，验证无串口、无图形环境时的接收管线。`receiver_tests` 保留虚拟串口和 QML 集成验证。应用构建目标与 F5 入口仍是 `appuart_tool`。

## 后续功能放在哪里

- TCP/UDP 等原始字节来源：在 `src/sources/` 添加实现及独立 CMake 目标，通过 `ByteSource` 接入现有会话；为新来源单独提供配置面板。
- 自定义串口协议：新增独立解析模块，增量消费字节源信号，维护自己的组帧状态，输出类型化数据；原始日志显示可同时保留。
- 波形／记录：以通道、数值和时间戳模型为输入，独立维护自己的历史与显示策略。
- ROS 话题：未来新增可选 ROS 适配模块，将结构化消息映射到类型化通道模型。不要为了复用字节日志接口把 ROS 消息强制序列化成字节；当前没有引入 ROS 依赖或预先规定 ROS 版本。

新增数据源、解析器、图表时优先增加对应模块，并在装配入口连接；不要把新职责重新塞进串口驱动或 Main.qml。

## 设置与串口列表

`AppSettings` 负责持久化外观偏好，`SettingsPage` 绑定设置对象。串口配置模型不再依赖偏好模块，直接列出 `SerialSource::availablePorts()` 提供的全部端口；USB 筛选及开关已删除。连接期间保留当前端口，断开后可刷新列表。设置与接收数据独立；测试使用临时 INI 目录隔离配置。

## 协议解析模块

- `ProtocolCatalog`：未启用解析、CSV、字典和自定义二进制的稳定标识、名称和说明。
- `ReceivePanel` 管理原始数据与协议页签。页签状态只影响显示，字节源持续向原始日志和已启用的协议会话分发数据。
- `BinaryConfig`：配置结构、类型宽度、长度规则和交叉字段校验；无串口或界面依赖。
- `ProtocolParser`：增量字节输入和帧输出接口；CSV 和 Binary 两个实现分别负责行组帧、帧头／长度组帧、字段解码与校验。
- `ProtocolSession`：选择和重建解析器，接收原始字节，维护帧统计及最新值，50 ms 合并通知；负责经过验证的二进制配置保存，不持有串口设备。
- `ProtocolPanel`：协议选择、统计和最新字段表；`BinaryProtocolEditor`：可视化配置草稿，保存时调用会话验证，失败保留草稿并显示原因。
- `RawReceivePanel` 保持原始日志，`ReceivePanel` 在同一页面装配协议选择、解析结果和原始日志。`ApplicationServices` 将 ByteSource 分别连接到原始会话和协议会话。
- 原始会话清空后发 `cleared`，装配层同步重置解析器的半帧和计数。协议切换只清空解析状态，不影响原始历史；配置修改仅在正在使用 binary 时重建当前解析器。
- `protocol_core` 仅链接 Qt Core，解析器输出保留字段类型和值，原始二进制区使用 QByteArray，数值数组使用 QVariantList。UI 通过预览字符串显示大数组，完整字段值仍在模型中。
- 当前没有 ROS 依赖；ROS 的结构化消息未来直接映射到通道模型，不通过字节解析器绕行。

规则、长度公式、边界和报文示例见 [protocols.md](protocols.md)。`protocol_parser_tests` 独立验证解析和配置；串口与编辑器通过 `receiver_tests` 验证。

## 模板与精细协议扩展

`FrameStructure` 是 Qt Core 层的通用布局编译器：将有序结构部分编译成 `BinaryConfig` 的偏移、帧长、校验范围及消息布局。`FrameStructureEditor` 只编辑结构数据，不自己计算解析规则。长度／命令宽度和顺序均由配置决定；公共字段、前缀校验、尾部校验与固定 HEX 帧尾均由同一个编译入口处理。`BinaryConfig` 持久化原始结构和编译结果，解析器只消费编译结果；更改字段后再次编译，避免偏移失效或公共字段重复。

结构模式的校验规则自动定位，独立 `ChecksumRuleEditor` 在该模式隐藏结果偏移；常用算法通过数据预设填写参数，自定义 CRC 仍经过相同算法实现。旧手动布局仍可读取，并可由界面推导为结构草稿；推导或编译错误只影响草稿，运行中的配置保持有效。

`BinaryConfig` 向后兼容原有配置，新增独立帧尾布局、多条 `ChecksumRule` 及 `MessageLayout`。帧尾长度不随校验启用状态改变；规则可以先验证固定前缀，再验证完整帧。命令路由和未知命令的原始载荷回退都在通用二进制解析器中完成。

`templates/*.json` 提供起步配置数据，`binarytemplates.cpp` 只读取资源，没有为某个项目在串口驱动里硬编码分支。`ChecksumRule` 使用标准多项式并统一处理反转；模板与高级表单最终都经过同一份配置验证。

`BinaryProtocolEditor` 只管理模板和数据字段，不再提供手动字段偏移或另一套校验控件；自动布局计算偏移和长度，消息类型在结构编辑器管理。独立的 `ChecksumRuleEditor` 提供算法预设及可选自定义参数。模板 JSON 只保存数据，不执行用户脚本。模板选择／编辑不影响运行中的解析器，应用有效配置才重建解析会话。

测试包含从 MCU 原始 `host_protocol_pack` 与 CRC 源码生成的固定报文向量、CRC 已知向量、关闭校验仍保留帧尾、未知命令、模板字段命名及高级表单保存。没有修改 MCU 工程。

## 文件模板库

`ProtocolTemplateStore` 独立负责模板文件的读写、校验、列表与错误状态，仍仅依赖 Qt Core。`ProtocolSession` 持有模板库，模板操作不调用解析器重建或应用配置。QML 通过文件对话框获得本地 URL；导入先验证版本和 `BinaryConfig`／`FrameStructure`，再以新标识原子写入用户模板目录，同名加序号，避免覆盖原模板。内置通用模板从只读 Qt 资源加载；`builtin:` 标识拒绝覆盖和删除。用户目录只管理个人 JSON，包含新建、导入和从内置模板保存的副本。兼容旧文件时隐藏未修改的通用起步副本，保留已修改的旧模板及网球协议。

原始配置快照仍由 `ProtocolSession` 的 QSettings 管理。模板库是外部 JSON 文件集合，与当前活动配置互不依赖；删除当前选中模板不会停止接收。测试使用临时 AppDataLocation，避免改动用户模板，另有独立 `protocol_template_tests` 验证文件往返、重命名、非法输入和删除后恢复行为。

## 命令独立结果

`BinaryChannels` 为当前模板中的每个命令构建独立解析器和单帧缓存；每个解析器校验完整帧后过滤命令字，其他有效命令不作为错误。`ProtocolSession` 只向当前选中命令的解析器交付字节，其他命令不更新。切换命令会清空结果及半帧状态，从新的输入开始。缓存规模随配置的命令数（最多 16）固定，不保存帧历史。

统一的协议编辑页由模板管理、内嵌的 `FrameStructureEditor` 和数据区内的 `DataFieldsEditor` 组成。数据区作为结构模块添加、删除和移动，字段模型由帧结构编辑器持有，视图重建不丢失草稿。命令字模块拥有独立下拉框，切换前保存当前命令字段，再载入所选命令字段，不再显示另一套数据类型选择。模板持久化保留全部命令，`commandValue` 表示配置时当前选中项；运行时查看选择不单独写入设置。

多模块帧由 `FrameStructure::compileMultiple` 单独编译，按模块顺序计算字段位置和多个 `commandMatches`，接收解析器在校验完整帧后匹配所有命令值。每个命令模块保存自己的 `messages`；数据区在 `fields` 保存当前字段，`variants` 按所属命令值保存字段配置。编辑器为每个数据区持有独立 ListModel，重排或切换命令不会复用其他区域的字段模型。单命令、单数据区继续输出兼容的旧配置；多模块接收选择由 `ProtocolSession::selectBinaryCommand` 更新对应模块及其数据区，重建当前解析器但不修改模板文件。

`DockPanel.qml` 负责停靠容器、浮动 ApplicationWindow 和标题栏操作；`DockWindowController` 位于 `uart_ui`，只跟踪标题栏输入、检测全局边缘或空位并发出吸附请求，不依赖串口或协议层。面板通过改变同一个框架 Item 的视觉父级实现浮动，原有 SerialConfigPanel、ReceivePanel 和 C++ 会话对象不重建。主窗口使用 SplitView 分配停靠宽度。

拖出手势在原窗口中保持独立的标题输入区域，视觉框架可移动到浮动窗口而不重置鼠标抓取。`DockWindowController` 使用原窗口的捕获及全局鼠标事件统一更新浮动位置和四向预览；自定义标题拖动不再跨窗口调用系统移动，也不通过按钮状态轮询提前结束。释放后延迟激活浮动窗口，避免原窗口处理释放事件时把它压到后面。

时序图的数据路径为 `ProtocolSession::frameDecoded → NumericStream → TimeSeriesPlot`。解析器每个成功帧都发出数值观察信号，二进制命令仅从当前选中分支采集。`numeric_data` 仅链接 Qt Core 与协议核心，管理字段发现、有限数值筛选、单调时钟和有上限的历史；`TimeSeriesPlot` 位于 UI 层，缓存绘制快照、按像素缩减折线，50 ms 合并刷新。解析状态重置时清空数值通道，不向界面提供模拟参数。

`ComponentMenu` 通过 UI 层的 `ComponentDragController` 保持原窗口鼠标捕获并检测当前组件区域；成功放置时连同目标区域内的松手坐标交给 `ComponentWorkspace`，由其转换为画布坐标（包含滚动偏移），以该位置为组件左上角，再通过 Loader 创建独立 `TimePlotCard`。组件区另保留 MIME 类型的 DropArea 扩展入口。每张卡片独立选择曲线，共享数值历史。新增组件应扩展组件菜单与对应组件工厂，不修改串口驱动。暂停仅冻结图表快照，接收及有限历史采集继续。


三面板及组件交互分工：

- `Main.qml` 仅装配连接／组件菜单、组件工作区和接收区；`DockLayout.qml` 管理三面板的嵌套 SplitView 与四向停靠，重排保留现有面板实例及会话。
- `ComponentWorkspace.qml` 管理组件工厂、画布位置／大小、层次和最大化；`ComponentFrame.qml` 是所有组件共用的标题、操作槽和移动／八向拉伸手势外框。`TimePlotCard.qml` 只负责时序图参数、图例和恢复自动视野操作。后续组件统一使用外框及工作区的几何接口。
- `plotting/PlotViewport` 是不依赖 QML 的坐标视野状态和变换，管理实时跟随、自动量程、指针锚定缩放与平移；`TimeSeriesPlot` 转换鼠标事件、生成绘制快照和渲染。手动视野独立于新样本刷新，重置解析会话时恢复默认。


浮动面板统一使用 `DockPanel` 的应用标题栏（带最大化／还原能力标志的 frameless Window 窗口），不再叠加系统标题栏。避免使用桌面可能拒绝最大化尺寸请求的 Tool 类型，保留主窗口 transientParent 关系。浮动前确定最终尺寸与位置；窗口控制和系统边缘缩放也在同一组件内装配。`DockWindowController` 仅跟踪原输入窗口的鼠标事件，不再轮询猜测系统标题栏是否在拖动；移动过程中不反复 raise 或变更窗口 flags，保留原窗口捕获直至释放，吸附仍通过既有布局接口完成。


贴靠目标由 `DockWindowController::targetAt` 命中：工作区外沿对应整体“大框”，内部对应鼠标下的已停靠面板“小框”，不再保留或接受空位。`DockPanel` 浮动时隐藏其布局容器，`DockLayout` 让其余面板铺满；鼠标捕获条在拉出前转到原窗口内部，保留拖动输入而不留布局占位。

预览阶段只计算 `guideRect` 和 `previewRect`，不修改面板可见性、尺寸、父节点或布局顺序。只有位于目标边缘中点附近的小范围（垂直边缘距离不超过 40 逻辑像素，沿边距中点不超过 32 逻辑像素，小面板按比例缩小）才显示对应标识，`DockOverlay` 在主窗口内绘制透明内部的边框。矩形按目标减去 12 像素分隔条后对半计算；松手才交给 `DockLayout`，整体贴靠重排根节点，局部贴靠替换目标叶子并保留旁边面板的方位。高度可缩至分配的一半；宽度保留面板的最小值（数据接收为 380 逻辑像素），触及下限时控制器同步调整预览宽度，避免出现工具栏横向滚动或实际落点偏离预览。

窗口原始鼠标事件只处理一次，忽略 Qt Quick 向 MouseArea 转发的重复事件。输入捕获、预览与布局分别由控制器、预览 Item 和布局组件负责；接收、协议、图表实例不会因贴靠重建。


`DockPanel.fixedSidebar` 为连接／组件菜单启用固定侧栏策略：停靠宽度固定，浮动时固定当前尺寸、隐藏缩放手柄和最大化按钮，仅接受主工作区左右侧。策略由通用面板属性驱动；命中与预览由控制器限制，实际布局由 DockLayout 再次校验。普通面板的全局目标矩形排除已停靠侧栏，避免其他面板的上下贴靠把侧栏挤入中间。


侧栏折叠是 `DockPanel.sidebarCollapsed` 的独立显示状态，展开宽度保持不变。`SidebarFoldHandle` 是原生 SplitView 外的独立输入条，24 逻辑像素阈值触发单次状态切换，输入条始终保留在同一窗口；侧栏从固定宽度直接切到零宽，仅保留原 12 像素分隔条作为展开入口。布局及贴靠矩形共同读取有效宽度与分隔条宽度，折叠不会断开串口或重建控件。


发送分层：`SendPanel.qml → SendSession → ByteSink → SerialSource/QSerialPort`。`send_core` 仅依赖 Qt Core，负责 UTF-8、严格 HEX 校验、结尾选择、单次大小上限与状态；`ByteSink` 是独立字节输出接口，不修改只负责输入生命周期的 ByteSource。SerialSource 以 ReadWrite 打开，负责实际异步写入及待写队列上限，接收链保持原有实现。ApplicationServices 装配发送会话并转发连接变化，main 只向 QML 暴露会话。QuickSendCard 装配 SendPanel，文本、HEX 和字典共用发送按钮和计时器。字典输入由 ParameterSendEditor 编码。 SendSession 将串口写队列接受的字节通过 ApplicationServices 交给原始日志，ReceiveSession 为日志保存 RX／TX 方向与时间戳。协议会话仍单独从 ByteSource 接收字节。
