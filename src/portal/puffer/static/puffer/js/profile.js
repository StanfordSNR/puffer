'use strict';

function share(token, button, csrf_token) {
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/accounts/share_token/');
  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
  var params = encodeURI('token=' + token +
                         '&share=true&csrfmiddlewaretoken=' + csrf_token)
  xhr.send(params);

  var list_item = button.parentNode.parentNode.parentNode;
  document.getElementById("unshared").removeChild(list_item);
  button.onclick = function() {
    unshare(token, button, csrf_token);
  }
  button.innerHTML = "mark unshared";
  document.getElementById("shared").appendChild(list_item);
}

function unshare(token, button, csrf_token) {
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/accounts/share_token/');
  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
  var params = encodeURI('token=' + token +
                         '&share=false&csrfmiddlewaretoken=' + csrf_token)
  xhr.send(params);

  var list_item = button.parentNode.parentNode.parentNode;
  document.getElementById("shared").removeChild(list_item);
  button.onclick = function() {
    share(token, button, csrf_token);
  }
  button.innerHTML = "mark shared";
  document.getElementById("unshared").appendChild(list_item);
}
