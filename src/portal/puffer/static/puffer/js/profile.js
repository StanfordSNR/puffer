'use strict';

function share(token, link, csrf_token) {
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/accounts/share_token/');
  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
  var params = encodeURI('token=' + token +
                         '&share=true&csrfmiddlewaretoken=' + csrf_token)
  xhr.send(params);

  // move the token
  var row = link.parentNode.parentNode;
  document.getElementById("unshared-tokens").removeChild(row);
  link.onclick = function() {
    unshare(token, link, csrf_token);
  }
  link.innerHTML = "mark unshared";
  document.getElementById("shared-tokens").appendChild(row);
}

function unshare(token, link, csrf_token) {
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/accounts/share_token/');
  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
  var params = encodeURI('token=' + token +
                         '&share=false&csrfmiddlewaretoken=' + csrf_token)
  xhr.send(params);

  // move the token
  var row = link.parentNode.parentNode;
  document.getElementById("shared-tokens").removeChild(row);
  link.onclick = function() {
    share(token, link, csrf_token);
  }
  link.innerHTML = "mark shared";
  document.getElementById("unshared-tokens").appendChild(row);
}
