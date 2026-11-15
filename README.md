# Lounge Sokuji / MK8DX QuickScore

Windows版OBSの中でマリオカート8DXの12人戦を集計するプラグインです。リザルトをCPUで検出し、名前の先頭文字をTypeSafe Jev画像APIへ問い合わせます。点数・順位・点差はアプリ側で計算します。現在のソースは **0.8.1** です。

設定形式は2v2 / 3v3 / 4v4 / 6v6。検証した画像・形式の範囲は[テスト資料](docs/TESTING.md)を確認してください。

## 引き継ぎ資料

| 目的 | 資料 |
| --- | --- |
| 別のAI・開発者が引き継ぐ | [現在の状態と次の作業](docs/HANDOFF.md)、[作業ルール](AGENTS.md) |
| 検出・API・保存を理解する | [設計](docs/ARCHITECTURE.md) |
| テストと実API試験 | [テスト手順・実測](docs/TESTING.md) |
| 配布物を作りOBSへ反映する | [配布と反映](docs/DEPLOYMENT.md) |
| 変更経緯 | [CHANGELOG](CHANGELOG.md) |

## 操作

1. 配布物の`install.cmd`を、OBSを終了した状態で管理者として実行します。既存プラグインはバックアップされます。cloneしたソースだけではインストールできないため、開発者は下記のビルド・配布手順を先に行います。
2. OBSの「ドック」→「MK8DX QuickScore」を開きます。
3. 元の16:9ゲーム映像、形式、カンマ区切りのチームタグを設定します。**先頭のタグが自チーム**です。ITF固定ではなく、Aや日本語なども使えます。
4. 「Jev API / TypeSafe」のAPIキー欄へ入力し「APIキーを保存」を押します。Windows資格情報マネージャーへ保存し、入力欄は空になります。保存だけでは画像を送りません。
5. 「Jev APIでリザルトを判定する」をONにし「集計開始」を押します。ローカルGPUモデルやPythonプロセスは不要です。最初にリザルト以外を3秒映すと自動待機します。既に結果が出ている場合は「現在のリザルトを確認」を使います。
6. 初期設定では12人のチームを確認してから「確認した内容で集計」を押します。読めない行は未選択です。修正するか「この結果をスキップ」で進めます。
7. 自動集計する場合は停止中に「検証を通った結果を自動集計する」をONにします。全12順位・confidence・各チーム人数・合計82点を満たす結果だけ加算します。

チーム間で先頭文字が重複する設定は拒否します。識別キーでは英字の大小文字と同音のひらがな・カタカナを同一視し、濁点・半濁点は区別します。読めない行を残り人数から補完しません。

## APIへ送るものと料金

ゲーム画面全体は送りません。1920×1080へ正規化したフレームから、各順位の名前先頭 **92×54px** だけを切り抜きます。12人分を1.5倍に拡大し、順位ラベル付き **480×448px** の1枚にまとめます。1回のアップロードに12個のChoice質問を付けます。

0.8.1では、0.8.0の長文ルールを共通化し、名前の後半・余白を減らしました。実2v2画像で約6,164→約2,460入力トークン、約60%削減しています。さらに小さい画像も試しましたが、日本語タグの不明行が増えたため採用していません。

通常は1結果につき自動要求1回。エラー時の自動再送なし、最大15秒です。停止後の回答は加算しません。手動の再判定ボタンは追加のAPI利用になります。料金はTypeSafeアカウントの条件が適用されます。公式単価での計算例と実測は[テスト資料](docs/TESTING.md)にあります。

APIをOFFにすると、手動確認した名前の見本によるCPU照合も利用できます。未確認のAPI判定を見本へ学習させません。

## 出力・Discord・保存

次の出力形式を維持します。

```text
【M】17／う 17 (±0)／L 16 (+1)／A 14 (+3)／B 9 (+8)／R 9 (+8)／現在1位／＠11
```

`score.txt`は同じ行をUTF-8・末尾改行なしで保存します。OBSスコアボードも同じ集計を表示します。Undoと履歴を利用できます。

DiscordはBotトークンとチャンネルIDを設定し、「Discordへ送信」を押したときだけ送ります。設定保存や集計だけでは送りません。Botには送信先の閲覧・メッセージ送信権限が必要です。

通常の設定・履歴は `%APPDATA%\obs-studio\plugin_config\mk8dx-quickscore`。検出時の全体PNG・順位一覧PNG・情報JSONは、その中の`captures`へ保存します。「リザルト画像の保存フォルダーを開く」で開けます。保留・スキップ・停止でも画像は残ります。APIへ送る名前画像と、ローカルへ保存する全体画像は別です。

APIキーとBotトークンは別々のWindows資格情報へ保存します。`TYPESAFE_API_KEY`環境変数も使えます。キー・PAT・設定・ユーザー履歴・実リザルト画像は、この公開リポジトリへ含めません。

## 開発環境

- Windows x64、Visual Studio 2022のC++開発ツール、Windows SDK
- CMake 3.23以降、Ninja（VS同梱版も利用可）
- Qt6 SDK: Core / Gui / Widgets / Network、Windowsプラグイン、TLSバックエンド
- OBSのヘッダーとimport libraryを準備したSDKディレクトリ
- 必要に応じて対象Windows SDKの生成済みC++/WinRTヘッダー

確認環境はOBS 32.2.2 / 実行時Qt 6.11.1。ビルドSDKはOBS 31.1.1 / Qt 6.8.3です。実HTTPS通信はQt 6.11.2のSchannelバックエンドで確認しています。OBS本体のQt DLLは置き換えません。別の組み合わせへ配布する場合は、その環境でTLS・ドック・映像取得を検証してください。

`DepsRoot`には、`include/obs.h`、`include/obs-module.h`、`include/obs-frontend-api.h`と関連ヘッダー、`lib/obs.lib`、`lib/obs-frontend-api.lib`が必要です。通常のOBSインストールディレクトリそのものとは異なります。`buildspec.json`に参照したOBS・Qt依存パッケージの版・URL・ハッシュを記録しています。依存SDK・モデルはGitへコピーしません。

## ビルド

以下のパスは各環境に置き換えます。`build.ps1`はMSVC・Windows SDK・Ninjaの環境をプロセス内で設定します。VSエディションが違う場合は`-VsRoot`を指定します。

```powershell
.\build.ps1 -DepsRoot C:\dev\obs-sdk `
  -QtRoot C:\dev\qt6-sdk `
  -CppWinrt C:\dev\cppwinrt-generated `
  -BuildDir .\build
ctest --test-dir .\build --output-on-failure
```

C++/WinRTの生成ヘッダーがWindows SDKで十分な場合は`-CppWinrt`を省略できます。生成が必要ならMicrosoft.Windows.CppWinRTの`cppwinrt.exe`で対象SDKのprojectionを生成して渡します。

ヘッダー・クラスのレイアウト変更後は`-Clean`でビルドします。この環境ではMSVCの日本語include出力によりNinjaの依存検出が不完全になる場合がありました。ビルド成功と、OBS再起動後の読み込み成功は別に確認します。

## 主なソース

| ファイル | 役割 |
| --- | --- |
| `src/plugin-main.cpp` | OBSへの登録とドック生成 |
| `src/ui/main-dock.cpp` | UI、ワーカー、ゲート、API要求、確認と集計 |
| `src/obs/frame-capture.cpp` | OBS元映像の取得 |
| `src/core/analysis.cpp` / `src/reference/recognizer.cpp` | 順位・加点検出、タグ規則、出力整形 |
| `src/core/jev-api-reader.cpp` | 送信画像、Choice要求、HTTP、応答検証 |
| `src/core/result-gate.hpp` | 安定確認、起動保護、重複防止 |
| `src/core/result-capture.cpp` | 検出フレーム保存 |
| `src/core/scoring.cpp` / `session-store.cpp` | 点数、人数検証、原子的な履歴保存 |
| `src/core/credential-store.cpp` / `discord-sender.cpp` | 秘密保管とDiscord送信 |
| `src/core/fast-reader.cpp` | APIをOFFにした場合のCPU照合 |

## ライセンスと外部仕様

プラグインは[GPLv2](LICENSE)。Qt TLSのライセンスと対応ソース案内は[third-party/qt-tls](third-party/qt-tls)にあります。旧Visual JevコードのライセンスはそのディレクトリのNOTICEを参照してください。現在のAPI経路は旧GPUワーカーを使いません。

TypeSafeの[画像仕様](https://docs.typesafe.ai/images)、[モデル・料金](https://docs.typesafe.ai/models)、[OpenAPI](https://api.typesafe.ai/openapi.json)に基づきます。画像機能は試験的機能として案内されており、全ての文字・レースでの認識率を保証するものではありません。初期設定の手動確認を残しています。
