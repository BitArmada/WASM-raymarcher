var canv = document.getElementById('canv');
var ctx = canv.getContext('2d');

var dataLog = document.getElementById('data');

canv.width = window.innerHeight;
canv.height = window.innerHeight;

canv.style.left = (window.innerWidth/2) - (canv.width/2) + 'px';

var WIDTH = 50;
var HEIGHT = 50;

var data = null;

var progress = 0;

var renderTime = null;
var start = 0;

var src = null;

const importObject = {
    env: {
        displayProgress: function(p){
            progress = p;
            displayData();
        },
        // print: console.log,
    }
}

render();

function displayData(){
    dataLog.innerHTML = '';
    dataLog.innerHTML += Math.round(progress*1000)/10+'% COMPLETE\n';
    dataLog.innerHTML += 'WIDTH: '+WIDTH+'\n';
    dataLog.innerHTML += 'HEIGHT: '+HEIGHT+'\n';
    var time = renderTime ?? (performance.now() - start);
    dataLog.innerHTML += 'TOOK: '+time+' ms\n';
}

function imagedata_to_image(imagedata) {
    var canvas = document.createElement('canvas');
    var ctx = canvas.getContext('2d');
    canvas.width = imagedata.width;
    canvas.height = imagedata.height;
    ctx.putImageData(imagedata, 0, 0);

    var image = new Image();
    image.src = canvas.toDataURL();
    return image;
}

function render(){
    var size = document.getElementById('size').value;
    WIDTH = size;
    HEIGHT = size;
    var steps = document.getElementById('steps').value;
    var samples = document.getElementById('samples').value;
    var bounces = document.getElementById('bounces').value;
    var spread = document.getElementById('spread').value;
    WebAssembly.instantiateStreaming(fetch('./main.wasm'),  importObject)
    .then(
        program => {
            var instance = program.instance;
            var module = program.module;

            data = new Uint8ClampedArray(instance.exports.memory.buffer, 10000, WIDTH*HEIGHT*4)

            // console.log(instance.exports.draw(data.byteOffset));
            instance.exports.setConstants(steps, samples, bounces, spread);
            instance.exports.initObjects();

            start = performance.now();
            console.time('render');
            console.log(instance.exports.draw(data.byteOffset, WIDTH,HEIGHT));
            var time = console.timeEnd('render');
            var end = performance.now();
            renderTime = end-start;

            ctx.fillStyle = "black";
            ctx.fillRect(0,0,canv.width, canv.height);

            var img = new ImageData(data, WIDTH,HEIGHT);

            var image = imagedata_to_image(img);
            document.getElementById('img').src = image.src;

            src = image.src;

            img = scaleImageData(img, Math.floor(canv.height/HEIGHT));
            ctx.putImageData(img, 0,0);

            displayData();
        }
    );

}

async function download(){
    const image = await fetch(src)
    const imageBlog = await image.blob()
    const imageURL = URL.createObjectURL(imageBlog)

    const link = document.createElement('a')
    link.href = imageURL
    link.download = 'render'
    document.body.appendChild(link)
    link.click()
    document.body.removeChild(link)
}

function scaleImageData(imageData, scale) {
    var scaled = ctx.createImageData(imageData.width * scale, imageData.height * scale);
  
    for(var row = 0; row < imageData.height; row++) {
      for(var col = 0; col < imageData.width; col++) {
        var sourcePixel = [
          imageData.data[(row * imageData.width + col) * 4 + 0],
          imageData.data[(row * imageData.width + col) * 4 + 1],
          imageData.data[(row * imageData.width + col) * 4 + 2],
          imageData.data[(row * imageData.width + col) * 4 + 3]
        ];
        for(var y = 0; y < scale; y++) {
          var destRow = row * scale + y;
          for(var x = 0; x < scale; x++) {
            var destCol = col * scale + x;
            for(var i = 0; i < 4; i++) {
              scaled.data[(destRow * scaled.width + destCol) * 4 + i] =
                sourcePixel[i];
            }
          }
        }
      }
    }
  
    return scaled;
}