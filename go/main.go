package main

import (
  "image"
	"image/color"
	"log"
  //"fmt"
	"os"
  "io"
  "context"

	"gioui.org/app"
	"gioui.org/op"
	"gioui.org/op/paint"
	"gioui.org/f32"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

  pb "pesaranlab.com/thalamus/proto-gen"
)

func main() {
  images := make(chan image.Image)

  go func() {
    conn, err := grpc.NewClient("localhost:50050", grpc.WithTransportCredentials(insecure.NewCredentials()))
    if err != nil {
      log.Fatalln(err)
    }
    defer conn.Close()

    c := pb.NewThalamusClient(conn)

    ctx := context.Background()

    request := pb.ImageRequest{
      Node: &pb.NodeSelector{Name: "Node 1"},
    }
    stream, err := c.Image(ctx, &request)

    if err != nil {
      log.Fatalln(err)
    }
    for {
      response, err := stream.Recv()
      if err != nil {
        log.Fatalln(err)
      }

      the_image := image.NewGray(image.Rectangle{Min: image.Point{0, 0}, Max: image.Point{int(response.Width), int(response.Height)}})
      if err == io.EOF {
        break
      }
      for y := 0;y < int(response.Height); y++ {
        for x := 0;x < int(response.Width); x++ {
          the_image.Set(x, y, color.Gray{Y:response.Data[0][x + y* int(response.Width)]})
        }
      }
      images <- the_image
    }
  }()

	go func() {
		window := new(app.Window)
		err := run(window, images)
		if err != nil {
			log.Fatal(err)
		}
		os.Exit(0)
	}()
	app.Main()
}

//func draw(ops *op.Ops) {
//}

func run(window *app.Window, images chan image.Image) error {
  to_drawer := make(chan app.FrameEvent)
  from_drawer := make(chan int)

  image_file, err := os.Open("C:\\Thalamus\\board.png")
  if err != nil {
    log.Fatalln(err)
  }

  image, _, err := image.Decode(image_file)
  if err != nil {
    log.Fatalln(err)
  }

  go func() {
	  var ops op.Ops
	  for {
      select {
      case e := <- to_drawer:
	  	  gtx := app.NewContext(&ops, e)

        paint.NewImageOp(image).Add(&ops)
        var scale float32
        scalex := float32(gtx.Constraints.Max.X)/float32(image.Bounds().Max.X)
        scaley := float32(gtx.Constraints.Max.Y)/float32(image.Bounds().Max.Y)
        scale = min(scalex, scaley)
        //fmt.Println(scale)
        op.Affine(f32.Affine2D{}.Scale(f32.Pt(0, 0), f32.Pt(scale, scale))).Add(&ops)
	      paint.PaintOp{}.Add(&ops)

	  	  e.Frame(gtx.Ops)

        from_drawer <- 0
      case image = <- images:
        window.Invalidate()
      }
	  }
  }()

  for {
	  switch e := window.Event().(type) {
	  case app.DestroyEvent:
	  	return e.Err
	  case app.FrameEvent:
      to_drawer <- e
      <-from_drawer
    }
  }
}
