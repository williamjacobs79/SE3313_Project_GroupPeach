// Elements
const signInButton = document.getElementById("sign-in");
const modal = document.getElementById("auth-modal");
const closeModalButton = document.getElementById("close-modal");
const loginForm = document.getElementById("login-form");
const createAccountForm = document.getElementById("create-account-form");
const createAccountButton = document.getElementById("create-account-btn");
const backToLoginButton = document.getElementById("back-to-login-btn");
const loginButton = document.getElementById("login-btn");
const createButton = document.getElementById("create-btn");
const userDisplay = document.getElementById("user-display");
const signOutButton = document.getElementById("sign-out");

// Load surf locations when the page first loads
document.addEventListener('DOMContentLoaded', () => {
    loadSurfLocations();
    updateAuthUI();
});

// Show Modal
signInButton.addEventListener("click", () => {
  modal.style.display = "flex";
});

// Close Modal
closeModalButton.addEventListener("click", () => {
  modal.style.display = "none";
});

// Switch to Create Account Form
createAccountButton.addEventListener("click", () => {
  loginForm.style.display = "none";
  createAccountForm.style.display = "block";
});

// Switch to Login Form
backToLoginButton.addEventListener("click", () => {
  createAccountForm.style.display = "none";
  loginForm.style.display = "block";
});

// Login event listener
loginButton.addEventListener("click", async () => {
  const username = document.getElementById("username").value;
  const password = document.getElementById("password").value;

  try {
    const response = await fetch("http://localhost:3000/api/login", {
      method: "POST",
      mode: 'cors',
      headers: { 
        "Content-Type": "application/json",
        "Accept": "application/json",
        "Origin": "http://localhost:8000"  // This header is kept for login
      },
      credentials: 'omit',
      body: JSON.stringify({ username, password }),
    });

    const result = await response.json();

    if (result.success) {
      localStorage.setItem("loggedInUser", JSON.stringify({ 
        userId: result.userId, 
        username: result.username 
      }));
      updateAuthUI();
      modal.style.display = "none";
    } else {
      alert(result.message || "Login failed. Please try again.");
    }
  } catch (error) {
    console.error("Error during login:", error);
    alert("An error occurred during login.");
  }
});

// Create account event listener
createButton.addEventListener("click", async () => {
  const newUsername = document.getElementById("new-username").value;
  const newPassword = document.getElementById("new-password").value;
  const newEmail = document.getElementById("new-email").value;

  try {
    const response = await fetch("http://localhost:3000/api/create-account", {
      method: "POST",
      mode: 'cors',
      headers: { 
        "Content-Type": "application/json",
        "Accept": "application/json",
        "Origin": "http://localhost:8000"  // This header is kept for create-account
      },
      credentials: 'omit',
      body: JSON.stringify({
        username: newUsername,
        password: newPassword,
        email: newEmail,
      }),
    });

    const result = await response.json();

    if (result.success) {
      alert("Account created successfully. You can now log in.");
      createAccountForm.style.display = "none";
      loginForm.style.display = "block";
    } else {
      alert(`Failed to create account: ${result.message || "Unknown error"}`);
    }
  } catch (error) {
    console.error("Error creating account:", error);
    alert("An error occurred while creating the account.");
  }
});

// Event listener for the "Surf Locations" button
document.getElementById("surf-locations-btn").addEventListener("click", () => {
  loadSurfLocations();
});

// Function to load surf locations and display them
async function loadSurfLocations() {
  const mainContent = document.getElementById("main-content");
  mainContent.innerHTML = `
    <div class="search-bar">
      <input type="text" id="search-country" placeholder="Search by Country">
      <input type="text" id="search-location" placeholder="Search by Location">
      <button id="search-btn">Search</button>
    </div>
    <div id="surf-locations" class="tiles-container"></div>
  `;

  // Fetch and display all surf locations on initial load
  await fetchAndDisplayLocations();

  // Add search functionality
  document.getElementById("search-btn").addEventListener("click", async () => {
    const country = document.getElementById("search-country").value.trim();
    const location = document.getElementById("search-location").value.trim();
    await fetchAndDisplayLocations(country, location);
  });
}

// Function to fetch and display surf locations
async function fetchAndDisplayLocations(country = "", location = "", filterLikes = false) {
  try {
    console.log('Fetching locations with:', { country, location });
    const response = await fetch(
      `http://localhost:3000/api/surf-locations?country=${country}&location=${location}&filterLikes=${filterLikes}`,
      {
        method: 'GET',
        headers: { 'Accept': 'application/json' }
      }
    );

    const locations = await response.json();
    console.log('Received locations:', locations);

    const tilesContainer = document.getElementById("surf-locations");
    if (!tilesContainer) {
      console.error('Could not find surf-locations container');
      return;
    }

    tilesContainer.innerHTML = ""; // Clear existing tiles

    if (!locations || locations.length === 0) {
      tilesContainer.innerHTML = `<p>No surf locations found.</p>`;
      return;
    }

    // Create tiles for each location
    locations.forEach((loc) => {
      const tile = document.createElement("div");
      tile.classList.add("tile");
      tile.innerHTML = `
        <h3>${loc.locationName || 'Unnamed Location'}</h3>
        <p>Break Type: ${loc.breakType || 'Not specified'}</p>
        <p>Surf Score: ${loc.surfScore || 'Not rated'}</p>
        <p>Country: ${loc.countryName || 'Not specified'}</p>
        <p>Added by User ID: ${loc.userId || 'Unknown'}</p>
        <p>Likes: ${loc.TotalLikes || 0}</p>
        <p>Comments: ${loc.TotalComments || 0}</p>
      `;
      tile.addEventListener("click", () => loadLocationDetails(loc.locationName));
      tilesContainer.appendChild(tile);
    });
  } catch (error) {
    console.error("Error fetching surf locations:", error);
    const tilesContainer = document.getElementById("surf-locations");
    if (tilesContainer) {
      tilesContainer.innerHTML = `<p>Error loading surf locations. Please try again.</p>`;
    }
  }
}

// Function to load location details and display posts and comment section
async function loadLocationDetails(locationName) {
  try {
    const response = await fetch(`http://localhost:3000/api/location-details?locationName=${locationName}`);
    const data = await response.json();

    const mainContent = document.getElementById("main-content");
    mainContent.innerHTML = ""; // Clear previous content

    // Check if user is logged in
    const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

    // Updated Create Post Section with nicer UI
    const createPostSection = loggedInUser 
      ? `
        <div class="create-post-section">
          <h3>Create a Post</h3>
          <form id="create-post-form">
            <textarea id="post-description" placeholder="Share your experience at ${locationName}..." required></textarea>
            <button type="submit" id="create-post-button">Post It!</button>
          </form>
        </div>
      ` 
      : `<p>You must be logged in to create a post.</p>`;

    // Check if data is valid
    if (!data || !Array.isArray(data)) {
      mainContent.innerHTML = `
        <h2>${locationName}</h2>
        ${createPostSection}
        <p>No data available for this location.</p>
      `;
      return;
    }

    if (data.length === 0) {
      mainContent.innerHTML = `
        <h2>${locationName}</h2>
        ${createPostSection}
        <p>No posts available for this location.</p>
      `;
      return;
    }

    // Display location details with posts
    mainContent.innerHTML = `
      <h2>${locationName}</h2>
      ${createPostSection}
      <h3>Posts:</h3>
      <div id="post-tiles" class="tiles-container"></div>
    `;

    // Display all posts
    const postTiles = document.getElementById("post-tiles");
    
    // Filter posts with valid _id
    const posts = data.filter(post => post && (post._id || (post._id && post._id.$oid)));
    
    if (posts.length === 0) {
      postTiles.innerHTML = `<p>No posts yet. Be the first to post!</p>`;
    } else {
      posts.forEach((post) => {
        const tile = document.createElement("div");
        tile.classList.add("tile", "post-tile");
        const postId = post._id?.$oid || post._id;
        tile.dataset.postId = postId;
        
        tile.innerHTML = `
          <p>${post.description || "No description provided"}</p>
          <p><strong>Posted by:</strong> ${post.userId || "Unknown"}</p>
          <p><strong>Likes:</strong> ${post.TotalLikes || 0}</p>
          <p><strong>Comments:</strong> ${post.TotalComments || 0}</p>
        `;
        postTiles.appendChild(tile);
      });

      // Attach event listeners for post tiles to load post details
      addPostTileEventListeners();
    }

    // Updated create post event listener
    if (loggedInUser) {
      const createPostForm = document.getElementById("create-post-form");
      if (createPostForm) {
        createPostForm.addEventListener("submit", async (e) => {
          e.preventDefault();
          const description = document.getElementById("post-description").value.trim();
          try {
            const response = await fetch("http://localhost:3000/api/create-post", {
              method: "POST",
              headers: { 
                "Content-Type": "application/json",
                "Accept": "application/json"
              },
              body: JSON.stringify({
                userId: loggedInUser.userId,
                locationName: locationName,
                description: description
              }),
            });
            const result = await response.json();
            if (result.success) {
              alert("Post created successfully!");
              // Reload the location details to show the new post
              loadLocationDetails(locationName);
            } else {
              alert(`Failed to create post: ${result.message || "Unknown error"}`);
            }
          } catch (error) {
            console.error("Error creating post:", error);
            alert("An error occurred while creating the post.");
          }
        });
      }
    }
  } catch (error) {
    console.error("Error loading location details:", error);
    const mainContent = document.getElementById("main-content");
    mainContent.innerHTML = `
      <h2>${locationName}</h2>
      <p>Error loading location details. Please try again.</p>
    `;
  }
}

// Function to like a comment
async function likeComment(commentId) {
  const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

  if (!loggedInUser) {
    alert("You must be logged in to like a comment.");
    return;
  }

  try {
    const response = await fetch("http://localhost:3000/api/like-comment", {
      method: "POST",
      mode: 'cors',
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ userId: loggedInUser.userId, commentId }),
    });

    const result = await response.json();

    if (result.success) {
      // Update the like count dynamically
      const likeCountElement = document.querySelector(`.like-count[data-comment-id="${commentId}"]`);
      if (likeCountElement) {
        likeCountElement.textContent = parseInt(likeCountElement.textContent) + 1;
      }
    } else {
      alert(`Failed to like comment: ${result.message || "Unknown error"}`);
    }
  } catch (error) {
    console.error("Error liking comment:", error);
    alert("An error occurred while liking the comment.");
  }
}

// Function to create a comment
async function createComment(postId, description) {
  const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

  if (!loggedInUser) {
    alert("You must be logged in to create a comment.");
    console.error("Error: No logged-in user found.");
    return;
  }

  console.log("Post ID:", postId);
  console.log("User ID:", loggedInUser.userId);
  console.log("Description:", description);

  try {
    const response = await fetch("http://localhost:3000/api/create-comment", {
      method: "POST",
      mode: 'cors',
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        postId,
        userId: loggedInUser.userId,
        description,
      }),
    });

    const result = await response.json();

    if (result.success) {
      alert("Comment created successfully!");
      loadPostDetails(postId); // Reload comments after creating a new one
    } else {
      alert(`Failed to create comment: ${result.message || "Unknown error"}`);
    }
  } catch (error) {
    console.error("Error creating comment:", error);
    alert("An error occurred while creating the comment.");
  }
}

// Function to attach event listeners for post tiles
function addPostTileEventListeners() {
  const postTiles = document.querySelectorAll(".post-tile");
  postTiles.forEach((tile) => {
    const postId = tile.dataset.postId;
    if (!postId) {
      console.error("Post ID is undefined for a tile.");
      return;
    }
    tile.addEventListener("click", () => loadLocationDetails(postId));
  });
}

// Function to load details for a specific post
async function loadPostDetails(postId) {
  try {
    console.log("Loading post details for ID:", postId);
    const response = await fetch(`http://localhost:3000/api/post-comments?postId=${postId}`);
    const result = await response.json();
    const comments = result.comments || [];

    console.log("Comments:", comments);
    console.log("Number of comments:", comments.length);

    const mainContent = document.getElementById("main-content");
    const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

    mainContent.innerHTML = `
      <h2>Post Details</h2>
      <button id="back-to-location" class="button">Back to Location</button>
      <div id="post-details"></div>
      <h3>Comments:</h3>
      <div id="comment-tiles" class="tiles-container"></div>
      ${
        loggedInUser
          ? `
          <h3>Add a Comment:</h3>
          <form id="create-comment-form">
              <textarea id="comment-description" placeholder="Write your comment here..." required></textarea>
              <button type="submit">Add Comment</button>
          </form>
        `
          : `<p>You must be logged in to add a comment.</p>`
      }
    `;

    // Add back button functionality
    document.getElementById("back-to-location").addEventListener("click", () => {
      loadSurfLocations();
    });

    const commentTiles = document.getElementById("comment-tiles");

    if (comments.length === 0) {
      commentTiles.innerHTML = `<p>No comments yet. Be the first to comment!</p>`;
    } else {
      comments.forEach((comment) => {
        const tile = document.createElement("div");
        tile.classList.add("tile");
        tile.innerHTML = `
          <p>${comment.commentDescription}</p>
          <p><strong>User ID:</strong> ${comment.userId}</p>
          <p><strong>Likes:</strong> <span class="like-count" data-comment-id="${comment.commentId}">${comment.TotalLikes || 0}</span></p>
          <button class="like-button" data-comment-id="${comment.commentId}">Like</button>
        `;
        commentTiles.appendChild(tile);
      });

      // Add like button listeners
      const likeButtons = document.querySelectorAll(".like-button");
      likeButtons.forEach((btn) => {
        btn.addEventListener("click", async () => {
          const commentId = btn.dataset.commentId;
          await likeComment(commentId);
        });
      });
    }

    if (loggedInUser) {
      const createCommentForm = document.getElementById("create-comment-form");
      if (createCommentForm) {
        createCommentForm.addEventListener("submit", async (e) => {
          e.preventDefault();
          const description = document.getElementById("comment-description").value.trim();
          await createComment(postId, description);
        });
      }
    }
  } catch (error) {
    console.error("Error loading post details:", error);
    const mainContent = document.getElementById("main-content");
    if (mainContent) {
      mainContent.innerHTML += `<p>Error loading post details. Please try again.</p>`;
    }
  }
}

function updateAuthUI() {
  const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

  if (loggedInUser) {
    userDisplay.innerText = `Welcome, ${loggedInUser.username}`;
    signInButton.style.display = "none";
    signOutButton.style.display = "inline-block";
  } else {
    userDisplay.innerText = "";
    signInButton.style.display = "inline-block";
    signOutButton.style.display = "none";
  }
}

signOutButton.addEventListener("click", () => {
  localStorage.removeItem("loggedInUser");
  updateAuthUI();
  alert("Signed out!");
  loadSurfLocations();
});
