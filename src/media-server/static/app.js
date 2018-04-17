function getWidgetUrl() {
  return '/widget.html';
}

function handleSignedInUser(user) {
  document.getElementById('user-signed-in').style.display = 'block';
  document.getElementById('user-signed-out').style.display = 'none';
  document.getElementById('user-prompt').textContent = 'Welcome! ' + user.displayName;
}

function handleSignedOutUser(user) {
  document.getElementById('user-signed-in').style.display = 'none';
  document.getElementById('user-signed-out').style.display = 'block';
  document.getElementById('user-prompt').textContent = 'Please sign in';
}

function initApp() {
  // Listening for auth state changes.
  firebase.auth().onAuthStateChanged(function(user) {
    if (user) {
      // User is signed in.
      handleSignedInUser(user);
    } else {
      // User is signed out.
      handleSignedOutUser(user);
    }
  });

  document.getElementById('sign-in').addEventListener('click',
    function() {
      window.location.assign(getWidgetUrl());
    }
  );

  document.getElementById('sign-out').addEventListener('click',
    function() {
      firebase.auth().signOut();
    }
  );
};

window.addEventListener('load', initApp);
