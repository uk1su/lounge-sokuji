# 設計と変更箇所

## フレームから得点まで

1. `src/plugin-main.cpp` がOBSドックとスコアボードソースを登録する。
2. `src/obs/frame-capture.cpp` が選択ソースの元映像を取得する。OBSのデスクトップ画面を撮影する方式ではない。125msの取得周期にはGPUの描画・転送待ち時間も含める。
3. `src/ui/main-dock.cpp::workerLoop` がワーカースレッドで `Analyzer::read(..., false, true)` を呼ぶ。1920×1080へ正規化し、順位・加点記号を画像照合する。通常のAPI経路ではOCRを呼ばない。
4. `ResultGate` は起動後にリザルト以外が連続3秒表示されたことを確認する。候補は3観測・250ms以上の安定を必要とする。既に表示中の結果は「現在のリザルトを確認」から扱える。
5. `MainDock::requestRecognition` がその結果を予約してゲートをロックし、同一フレームを `saveResultCapture` へ渡す。APIの失敗や弱い文字判定でゲートを解除してはいけない。
6. APIモードでは `JevApiReader::read` が同じフレームから名前画像を作り、非同期HTTPリクエストを1回送る。CPUモードでは確認済みの名前の見本を照合する。
7. コールバックは実行世代・結果サイクル・現在の名前画像を検証する。停止後の回答や、判定中に名前が変わった回答を自動加算しない。
8. 全12順位、タグ、チーム人数、confidenceを `Session` で検証し、82点になることを確認する。初期設定は手動確認。保存が成功してから状態を加算する。

## Jev API契約

- 固定URL: `POST https://api.typesafe.ai/v1/systemone`
- モデル: `jev-1.14.0`。エイリアスの自動更新により判定が変わることを避ける。
- `Authorization: Bearer ...` と `TypeSafe-Danger-Zone-Images: true` が必須。
- multipartの最初のpartは`request`（application/json）、次のpartは`montage`（image/png）。画像のUUID参照をstateに1回だけ置く。
- `rank1`〜`rank12`の12個のChoice質問。選択肢は`team0`〜と`unknown`。サーバーへ名前の全文を文字起こしさせたり、点数を計算させたりしない。
- 0.8.1では、共通ルールをstateへ移した。各質問は順位・行・列だけを指定し、各選択肢は先頭の表示文字だけを記す。繰り返す長文を減らす。
- 名前ROIは正規化画像の`x=1013, y=85+78*row, width=92, height=54`。1.5倍の138×81へ拡大し、3列4行の480×448画像へ配置する。各セル160×112、画像左上はセル内(6,23)。順位ラベルを付ける。
- 0.8.0は144×54のROIを2倍にし、960×576へ配置していた。0.8.1では名前の後半・余白を減らす。

公式画像APIの制限は各辺280〜1568px。個別の92×54切り抜きをそのまま送らず、1枚の画像へまとめる。PNGの圧縮率を変えるだけで画像トークンが減るとは限らない。実際の `usage.input_tokens` で比較する。

## 応答検証

`parseJevResponse` はモデル名、12回答、Choice形式、候補の確率範囲、確率合計、選択肢と最大確率の整合性を検証する。採用には以下すべてが必要。

- 最大確率0.90以上
- confidence 0.90以上
- 最大確率と次点の差0.25以上
- 選択肢が`unknown`ではない

これらはテストした正答率そのものではない。不明な行を空欄にし、各チームの残り人数から埋めない。HTTPエラーや不正な応答は手動確認へ移す。自動再送なし、最大15秒、レスポンス上限1MiB、リダイレクト追従なし。テスト用URL注入はHTTPの127.0.0.1だけを受け付ける。

## 画像保存と履歴

`src/core/result-capture.cpp` は全体1920×1080 PNG、順位一覧1040×945 PNG、情報JSONを原子的に保存する。順位一覧は(820,70)から切り抜く。保存画像とAPI用の画像は目的・サイズが違う。APIへ送るのは名前の画像のみ。

設定ディレクトリはOBSの `obs_module_config_path("")` から取得し、`captures`を一度だけ結合する。Windowsの通常設定は `%APPDATA%\obs-studio\plugin_config\mk8dx-quickscore`。二重にプラグイン名を結合しない。

`session-store.cpp` は履歴とrecognitionAuditをQSaveFileで保存する。監査情報には使用モデル・API使用量・画像への参照・認識行を含む。履歴や診断JSONにはローカルパスが含まれ得るため、公開リポジトリへ置かない。

## チームと出力

先頭グラフェムをNFKC・case fold・かなの対応で識別する。濁点・半濁点は保持する。モデルへ渡す表示文字は登録表記を保つ。自チームは設定順の先頭。

点数は固定の15,12,10,9,8,7,6,5,4,3,2,1。`formatScoreLine` は得点順、同点では登録順を維持する。`score.txt` はUTF-8・末尾改行なし。Discordは設定保存や集計だけでは送信せず、明示的な送信ボタンからだけ送る。

## 旧コードを触るとき

`local-reader.cpp` と `data/visual-jev` は以前のローカルGPUモデルのコード。旧テストと移行経緯のため残るが、現在のUIは `JevApiReader` を使う。API版の不具合を直すときに旧Pythonワーカーを変更しても現在の判定は変わらない。

公式資料: [画像](https://docs.typesafe.ai/images)、[モデルと料金](https://docs.typesafe.ai/models)、[OpenAPI](https://api.typesafe.ai/openapi.json)。画像機能は公式に試験的機能とされている。バージョン更新時はしきい値と実画像を再検証する。
