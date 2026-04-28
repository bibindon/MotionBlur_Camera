

https://github.com/user-attachments/assets/d41194c1-7273-4cfb-9efd-7434f9ac53a3

# DirectX9 Camera Motion Blur Sample

DirectX9 / D3DX9 / HLSL で、カメラ移動によるスクリーンスペース・モーションブラーを実装したサンプルです。
物体そのものの移動ブラーではなく、カメラの回転・前進・後退によって発生する画面上の速度を使ってブラーをかけます。

## 実行内容

- 画面サイズは 1600 x 900 です。
- 白黒の市松模様テクスチャを貼ったモデルを、10m 間隔のグリッド状に複数表示します。
- カメラは `(0, 0, -25)` を起点に、以下の動きを繰り返します。
  - その場で 360 度回転
  - 50m 直進
  - 50m 後退して開始位置へ戻る
- 回転と移動には SmoothStep による加減速を入れています。

## 操作

- `1` キー: モーションブラーの ON / OFF 切り替え

画面左上には FPS と操作説明を表示します。
モーションブラーの状態は大きな文字で表示され、ON は緑、OFF は赤です。

## 実装概要

`RenderPass1()` では MRT を使い、シーンを 2 枚のレンダーターゲットへ描画します。

- `COLOR0`: 通常カラー
- `COLOR1`: 深度グレースケール

`RenderPass2()` ではフルスクリーンクアッドを描画し、`simple2.fx` でポストエフェクトとしてモーションブラーを適用します。

モーションブラーでは、現在フレームの `ViewProjection`、前フレームの `ViewProjection`、現在フレームの逆 `ViewProjection` を使います。
深度RTから得た深度と現在UVからワールド座標を復元し、それを前フレームの `ViewProjection` で再投影して前フレームUVを求めます。
現在UVと前フレームUVの差を velocity として扱い、その方向へカラーRTを複数回サンプリングして平均します。

## 主なファイル

- `MultiPassRendering/main.cpp`: DirectX9 初期化、カメラ制御、MRT 描画、行列管理、FPS表示、ON/OFF入力
- `MultiPassRendering/simple.fx`: 通常カラーと深度を MRT に出力するシーン描画用エフェクト
- `MultiPassRendering/simple2.fx`: 深度から velocity を復元してモーションブラーをかけるポストエフェクト
- `MultiPassRendering/cube.x`: 表示モデル
- `MultiPassRendering/checker.png`: モデルに使用する白黒市松模様テクスチャ

## 調整ポイント

`MultiPassRendering/main.cpp` の先頭付近にある以下の定数で挙動を調整できます。

- `kBlurScale`: ブラー全体の強さ
- `kMaxBlurPixels`: 1フレームで許可する最大ブラー長
- `kCameraRotateDuration`: 360度回転にかける時間
- `kCameraMoveDuration`: 直進・後退にかける時間
- `kGridCountPerAxis`: 表示するモデル数
- `kDebugViewMode`: `0` は通常のモーションブラー表示、`1` は深度表示、`2` は velocity 表示、`3` は通常カラー表示

## 注意点

深度RTは `A8R8G8B8` にグレースケールとして保存しており、`simple2.fx` では `depthTexture.r` を DirectX の NDC 深度 `0..1` として扱っています。
そのため厳密な物理ベースの再構成ではありませんが、カメラ回転時の横方向ブラーと、前進・後退時の放射状ブラーを確認する用途には十分です。

`D3DPRESENT_INTERVAL_ONE` を使っているため、FPS は使用しているモニターのリフレッシュレートに依存します。
