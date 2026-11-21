  var Socket = new WebSocket('ws://' + window.location.hostname + ':81/');
  Socket.onopen = function(event){
    console.log("веб-сокет открыт");
  };
  Socket.onerror = function(event){
    console.log("ошибка веб-сокета");
  };
  Socket.onclose = function(event){
    console.log("веб-сокет закрыт");
  };
  Socket.onmessage = function(event){
    console.log("подключение через веб-сокет. Это передается ниже сообщения");
    console.log(event);
  };
  lastSend = 0;
  var FandBSliderElement = document.getElementById("ForwardAndBackwardRange");
  var FandBSliderOutput = document.getElementById("ValueOfForwardAndBackward");
  function sendFandBDirection(FandBSliderElement){
    var now = (new Date).getTime();
    if(lastSend > now -20)return;
    lastSend = now;
    console.log(FandBSliderElement);
    Socket.send("F" +FandBSliderElement);
  }
  var LeftandRightSliderElement = document.getElementById("LeftAndRightRange");
  var LandRSliderOutput = document.getElementById("ValueOfLeftAndRight");
  function sendLeftAndRight(LeftandRightSliderElement){
    var now = (new Date).getTime();
    if(lastSend > now -20)return;
    lastSend = now;
    console.log(LeftandRightSliderElement);
    Socket.send("L" +LeftandRightSliderElement);
  }