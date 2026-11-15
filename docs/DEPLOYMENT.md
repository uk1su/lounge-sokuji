# 配布とOBSへの反映

## 配布物を作る

cloneしたソースだけではinstall.cmdから導入できない。先にビルドし、以下の形にする。

```text
package/
  install.cmd
  install.ps1
  README.md
  LICENSE
  third-party/
  mk8dx-quickscore/
    bin/64bit/mk8dx-quickscore.dll
    bin/64bit/tls/qschannelbackend.dll
    data/...
```

```powershell
cmake -S . -B .\build `
  -DQUICKSCORE_TLS_BACKEND=C:\dev\qt-runtime\plugins\tls\qschannelbackend.dll
cmake --install .\build --prefix .\package\mk8dx-quickscore
Copy-Item install.cmd,install.ps1,README.md,LICENSE -Destination .\package
Copy-Item third-party -Destination .\package -Recurse
```

TLSバックエンドは**配布先OBSのQtと同じマイナーバージョン**を選ぶ。ビルドSDKのものをそのまま配布できるとは限らない。SDKのQt 6.8.3とOBSのQt 6.11.1を混ぜると問題があり、6.11系バックエンドで実HTTPS通信を確認した。OBSのQt DLLやqwindowsプラグインをSDK版へ置き換えて解決しない。

## 導入と確認

1. 単体・画像・必要なOBS統合テストを通す。
2. 配布物にキー・PAT・設定・履歴・画像がないことを確認する。ZIPを再展開し、DLLのSHA-256がビルド成果物と一致することも確認する。
3. OBSの終了を確認する。起動中はテスト済みパッケージまで準備し、利用者と終了のタイミングを調整する。使用中のDLLを差し替えない。
4. install.cmdを管理者として実行する。旧版は `%ProgramData%\OBS-Plugin-Backups\mk8dx-quickscore\日時` へ退避される。
5. `%ProgramData%\obs-studio\plugins\mk8dx-quickscore\bin\64bit\mk8dx-quickscore.dll` のハッシュを比較する。
6. OBSを再起動し、最新ログに `[MK8DX QuickScore] loaded (0.8.1 ...` があり、ドックが追加されたことを確認する。
7. 元映像・形式・タグ・APIキー状態を確認して開始する。起動だけでAPIやDiscordへ送信しない。

コピー成功、OBS読み込み、実ゲームの連続運用は、それぞれ別に確認する。

## ロールバック

OBSを終了し、バックアップのbin/dataを戻し、元のDLLハッシュを確認して再起動する。履歴とキーは通常プラグイン配置先とは別のユーザープロファイルにある。不要にリセット・削除しない。

## Gitとリリース

ソース・文書・非秘密のラベルだけをGitへ入れる。DLL・ZIP・PDB・依存SDK・実画像・生の監査JSONはGitへ入れない。バイナリを公開する場合は対応するソースのコミット、ライセンス、検証範囲を添える。リポジトリの公開範囲にも注意する。

コミット済みのソースから配布物を作る場合は、tools/package.ps1を使用できます。BuildDir、OutputDir、TlsBackendを指定すると、GitのコミットID・各ファイルのSHA-256をmanifest.jsonへ記録し、対応する全追跡ソースを同梱します。既存の配布フォルダーは削除しません。

Qt 6.11系のTLSバックエンドを取得する例: 公式Qt配布物、または `python -m pip download --no-deps PySide6_Essentials==6.11.2 -d C:\dev\qt-runtime-download` で対応するWindows wheelを取得し、ZIPとして展開したPySide6/plugins/tlsから選ぶ方法がある。実行先のQtと版が合うことをファイル情報で確認し、対応するQtライセンス・ソース案内も同梱する。wheel全体やQt本体をOBSへコピーしない。
