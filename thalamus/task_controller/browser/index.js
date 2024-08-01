function main() {
  let body = document.getElementById("body");
  let canvas = document.getElementById("canvas");
  let scale = ['iPhone', 'iPad', 'MacIntel'].includes(navigator.platform) ? window.devicePixelRatio : 1;
  let width = window.screen.width;
  let height = window.screen.height;
  if(['iPhone', 'iPad'].includes(navigator.platform)) {
    width *= scale;
    height *= scale;
    if(window.matchMedia('(orientation: landscape)').matches) {
      let temp = width;
      width = height
      height = temp;
    }
  }
  body.style.width = width + 'px';
  body.style.height = height + 'px';
  canvas.style.width = width + 'px';
  canvas.style.height = height + 'px';
  canvas.setAttribute('width', width);
  canvas.setAttribute('height', height);

  function requestFullscreen() {
    console.log('requestFullscreen')
    if(canvas.requestFullscreen) {
      return canvas.requestFullscreen();
    }
    if(canvas.webkitRequestFullscreen) {
      return canvas.webkitRequestFullscreen();
    }
  }

  body.addEventListener('mousemove', function(e) {
    e.preventDefault();
    if(websocket_open) {
      websocket.send(JSON.stringify({type: 'cursor', x: e.clientX, y: e.clientY, buttons: e.buttons}))
    }
  })
  body.addEventListener('mousedown', function(e) {
    e.preventDefault();
    if(websocket_open) {
      websocket.send(JSON.stringify({type: 'cursor', x: e.clientX, y: e.clientY, buttons: e.buttons}))
    }
  })
  body.addEventListener('mouseup', function(e) {
    e.preventDefault();
    requestFullscreen();
    if(websocket_open) {
      websocket.send(JSON.stringify({type: 'cursor', x: e.clientX, y: e.clientY, buttons: e.buttons}))
    }
  })
  body.addEventListener('touchmove', function(e) {
    e.preventDefault();
    if(websocket_open) {
      websocket.send(JSON.stringify({
        type: 'cursor',
        x: e.pageX,
        y: e.pageY, 
        buttons: 1
      }))
    }
  }, {passive: false})
  body.addEventListener('touchstart', function(e) {
    e.preventDefault();
    if(websocket_open) {
      websocket.send(JSON.stringify({
        type: 'cursor',
        x: e.pageX,
        y: e.pageY, 
        buttons: 1
      }))
    }
  }, {passive: false})
  body.addEventListener('touchend', function(e) {
    e.preventDefault();
    if(websocket_open) {
      websocket.send(JSON.stringify({
        type: 'cursor',
        x: e.pageX,
        y: e.pageY, 
        buttons: 0
      }))
    }
  }, {passive: false})
  document.addEventListener('contextmenu', function(e) {
    e.preventDefault()
  })
  
  let websocket = new WebSocket(`ws://${window.location.host}`);
  let websocket_open = false
  websocket.onopen = function() {
    console.log('onopen')
    websocket_open = true
    websocket.send(JSON.stringify({
      type: 'dimensions',
      width: width,
      height: height
    }))
  }

  //function animate() {
  //  if(document.last_touch) {
  //    let e = document.last_touch
  //    context = canvas.getContext("2d")
  //    context.fillStyle = 'rgb(255, 0, 0)';
  //    context.font = "30px Arial"
  //    context.fillRect(document.last_touch.pageX, document.last_touch.pageY, 100, 100)
  //    context.fillText(JSON.stringify({x: e.pageX, y: e.pageY, buttons: 0}) + navigator.platform, 100, 100)
  //  }
  //  requestAnimationFrame(animate)
  //}
  //requestAnimationFrame(animate)

  let commands = []
  let images = {}
  websocket.onmessage = function(event) {
    let message = JSON.parse(event.data)
    switch(message.function) {
      case '__enter__':
        commands = []
        break;
      case '__exit__':
        let context = canvas.getContext("2d")
        commands.forEach(function (message) {
          switch(message.function) {
            case 'fillRect':
              let old_fill_style = context.fillStyle
              if(message.args.length > 4) {
                context.fillStyle = `rgb(${message.args[4][0]}, ${message.args[4][1]}, ${message.args[4][2]})`
              }
              context.fillRect(message.args[0], message.args[1], message.args[2], message.args[3])
              context.fillStyle = old_fill_style
              break
            case 'drawImage':
              let image = images[message.args[4]]
              if(image !== undefined) {
                context.drawImage(image, message.args[0], message.args[1], message.args[2], message.args[3]);
              }
              break
          }
        })
        commands = []
        break;
      case 'fillRect':
        commands.push(message);
        break;
      case 'drawImage':
        commands.push(message);
        break;
      case 'clear_images':
        images = {};
        break;
      case 'store_image': {
        let image = new Image();
        image.src = message.args[1]
        images[message.args[0]] = image
        break;
      }
    }
  }
}

window.onload = main