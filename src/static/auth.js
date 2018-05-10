function init_app() {
  /* Listening for auth state changes */
  firebase.auth().onAuthStateChanged(function(user) {
    if (user) {
      /* User is signed in */
      document.getElementById('user-signed-in').style.display = 'block';
      document.getElementById('user-info').textContent = 'Welcome! ' + user.displayName;
    } else {
      /* Redirect to the sign-in page if user is not signed in */
      window.location.replace('/widget.html');
    }
  });

  document.getElementById('sign-out').addEventListener('click',
    function() {
      firebase.auth().signOut();
    }
  );
}

window.addEventListener('load', init_app);
