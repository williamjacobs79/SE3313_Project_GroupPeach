// elements
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
const addLocationButton = document.getElementById("add-location-btn");

// load surf locations when the page first loads
document.addEventListener("DOMContentLoaded", () => {
  loadSurfLocations();
  updateAuthUI();
});

// show modal (sign-in area)
signInButton.addEventListener("click", () => {
  modal.style.display = "flex";
});

// close modal (sign-in area)
closeModalButton.addEventListener("click", () => {
  modal.style.display = "none";
});

// switch to create account form
createAccountButton.addEventListener("click", () => {
  loginForm.style.display = "none";
  createAccountForm.style.display = "block";
});

// switch to login form
backToLoginButton.addEventListener("click", () => {
  createAccountForm.style.display = "none";
  loginForm.style.display = "block";
});

// helper function to get auth headers for API requests (from local storage)
function getAuthHeaders() {
  const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));
  const headers = {
    "Content-Type": "application/json",
    Accept: "application/json",
  };

  if (loggedInUser && loggedInUser.token) {
    headers["Authorization"] = `Bearer ${loggedInUser.token}`;
  }

  return headers;
}

// login event listener
loginButton.addEventListener("click", async () => {
  const username = document.getElementById("username").value;
  const password = document.getElementById("password").value;

  try {
    const response = await fetch("http://localhost:3000/api/login", {
      method: "POST",
      mode: "cors",
      headers: {
        "Content-Type": "application/json",
        Accept: "application/json",
        Origin: "http://localhost:8000",
      },
      credentials: "omit",
      body: JSON.stringify({ username, password }),
    });

    const result = await response.json();

    //store JWT inlocal storage
    if (result.success) {
      localStorage.setItem(
        "loggedInUser",
        JSON.stringify({
          userId: result.userId,
          username: result.username,
          token: result.token,
        })
      );
      updateAuthUI();
      modal.style.display = "none";
      loadSurfLocations();
    } else {
      alert(result.message || "Login failed. Please try again.");
    }
  } catch (error) {
    console.error("Error during login:", error);
    alert("An error occurred during login.");
  }
});

// create account event listener
createButton.addEventListener("click", async () => {
  const newUsername = document.getElementById("new-username").value;
  const newPassword = document.getElementById("new-password").value;
  const newEmail = document.getElementById("new-email").value;

  // email validation regex pattern
  const emailPattern = /^[a-zA-Z0-9._-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$/;

  // validate email
  if (!emailPattern.test(newEmail)) {
    alert("Please enter a valid email address.");
    return;
  }

  try {
    const response = await fetch("http://localhost:3000/api/create-account", {
      method: "POST",
      mode: "cors",
      headers: {
        "Content-Type": "application/json",
        Accept: "application/json",
        Origin: "http://localhost:8000",
      },
      credentials: "omit",
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

// "Surf Locations" button (refresh) event listener
document.getElementById("surf-locations-btn").addEventListener("click", () => {
  loadSurfLocations();
});

// "Add Location" button event listener
if (addLocationButton) {
  addLocationButton.addEventListener("click", () => {
    showAddLocationForm();
  });
}

// func to load and display surf locations
async function loadSurfLocations() {
  const mainContent = document.getElementById("main-content");

  // check if user is logged in to show add location button
  const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));
  const addLocationButton = loggedInUser
    ? `<button id="add-location-btn" class="button">Add New Location</button>`
    : "";

  mainContent.innerHTML = `
    <div class="search-bar">
      <input type="text" id="search-country" placeholder="Search by Country">
      <input type="text" id="search-location" placeholder="Search by Location">
      <button id="search-btn">Search</button>
      ${addLocationButton}
    </div>
    <div id="surf-locations" class="tiles-container"></div>
  `;

  // fetch and display all surf locations on initial load
  await fetchAndDisplayLocations();

  // add search functionality
  document.getElementById("search-btn").addEventListener("click", async () => {
    const country = document.getElementById("search-country").value.trim();
    const location = document.getElementById("search-location").value.trim();
    await fetchAndDisplayLocations(country, location);
  });

  // add event listener for the add location button if it exists
  const addLocBtn = document.getElementById("add-location-btn");
  if (addLocBtn) {
    addLocBtn.addEventListener("click", () => {
      showAddLocationForm();
    });
  }
}

// function to show the add location form
function showAddLocationForm() {
  const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

  if (!loggedInUser) {
    alert("You must be logged in to add a surf location.");
    return;
  }

  const mainContent = document.getElementById("main-content");
  mainContent.innerHTML = `
    <div class="form-container">
      <h2>Add New Surf Location</h2>
      <form id="add-location-form">
        <div class="form-group">
          <label for="location-name">Location Name:</label>
          <input type="text" id="location-name" required>
        </div>
        <div class="form-group">
          <label for="country-name">Country:</label>
          <input type="text" id="country-name" required>
        </div>
        <div class="form-group">
          <label for="break-type">Break Type:</label>
          <select id="break-type" required>
            <option value="">Select Break Type</option>
            <option value="Beach Break">Beach Break</option>
            <option value="Point Break">Point Break</option>
            <option value="Reef Break">Reef Break</option>
            <option value="River Mouth">River Mouth</option>
          </select>
        </div>
        <div class="form-group">
          <label for="surf-score">Surf Score (1-10):</label>
          <input type="number" id="surf-score" min="1" max="10" required>
        </div>
        <div class="form-actions">
          <button type="submit" class="button primary">Add Location</button>
          <button type="button" id="cancel-add-location" class="button secondary">Cancel</button>
        </div>
      </form>
    </div>
  `;

  // add event listener for form submission
  document
    .getElementById("add-location-form")
    .addEventListener("submit", handleAddLocationSubmit);

  // add event listener for cancel button
  document
    .getElementById("cancel-add-location")
    .addEventListener("click", () => {
      loadSurfLocations();
    });
}

// function to handle the add location form submission
async function handleAddLocationSubmit(event) {
  event.preventDefault();

  const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));
  if (!loggedInUser) {
    alert("You must be logged in to add a surf location.");
    return;
  }

  const locationName = document.getElementById("location-name").value.trim();
  const countryName = document.getElementById("country-name").value.trim();
  const breakType = document.getElementById("break-type").value;
  const surfScore = document.getElementById("surf-score").value;

  if (!locationName || !countryName || !breakType || !surfScore) {
    alert("All fields are required.");
    return;
  }

  try {
    const response = await fetch(
      "http://localhost:3000/api/protected/insert-surf-location",
      {
        method: "POST",
        headers: getAuthHeaders(),
        body: JSON.stringify({
          locationName,
          countryName,
          breakType,
          surfScore: surfScore.toString(),
          username: loggedInUser.username,
        }),
      }
    );

    if (response.ok) {
      alert("Surf location added successfully!");
      loadSurfLocations();
    } else {
      const errorText = await response.text();
      alert(`Failed to add location: ${errorText}`);
    }
  } catch (error) {
    console.error("Error adding surf location:", error);
    alert("An error occurred while adding the surf location.");
  }
}

// function to fetch and display surf locations
async function fetchAndDisplayLocations(
  country = "",
  location = "",
  filterLikes = false
) {
  try {
    console.log("Fetching locations with:", { country, location });
    const response = await fetch(
      `http://localhost:3000/api/surf-locations?country=${country}&location=${location}&filterLikes=${filterLikes}`,
      {
        method: "GET",
        headers: { Accept: "application/json" },
      }
    );

    const locations = await response.json();
    console.log("Received locations:", locations);

    const tilesContainer = document.getElementById("surf-locations");
    if (!tilesContainer) {
      console.error("Could not find surf-locations container");
      return;
    }

    tilesContainer.innerHTML = "";

    if (!locations || locations.length === 0) {
      tilesContainer.innerHTML = `<p>No surf locations found.</p>`;
      return;
    }

    // create tiles for each location
    locations.forEach((loc) => {
      const tile = document.createElement("div");
      tile.classList.add("tile");

      const postCount = loc.postCount !== undefined ? loc.postCount : 0;

      tile.innerHTML = `
        <h3>${loc.locationName || "Unnamed Location"}</h3>
        <p>Country: ${loc.countryName || "Not specified"}</p>
        <p>Break Type: ${loc.breakType || "Not specified"}</p>
        <p>Surf Score: ${loc.surfScore || "Not rated"}</p>
        <p>Added by User: ${loc.userId || "Unknown"}</p>
        <p>Posts: <span class="post-count">${postCount}</span></p>
      `;
      tile.addEventListener("click", () =>
        loadLocationDetails(loc.locationName)
      );
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

// function to load location details and display posts and comment section
async function loadLocationDetails(locationName) {
  try {
    const response = await fetch(
      `http://localhost:3000/api/location-details?locationName=${locationName}`
    );
    const data = await response.json();

    const mainContent = document.getElementById("main-content");
    mainContent.innerHTML = "";

    // check if user is logged in
    const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

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

    // check if data is valid
    if (!data || !Array.isArray(data)) {
      mainContent.innerHTML = `
        <h2>${locationName}</h2>
        <button id="back-to-locations" class="button">Back to Locations</button>
        ${createPostSection}
        <p>No data available for this location.</p>
      `;
      document
        .getElementById("back-to-locations")
        .addEventListener("click", loadSurfLocations);
      return;
    }

    // filter out the location data from posts data
    const locationData = data.filter((item) =>
      item.hasOwnProperty("breakType")
    );
    const postsData = data.filter((item) => item.hasOwnProperty("description"));

    // get the location information (should be the first item)
    const locationInfo = locationData.length > 0 ? locationData[0] : null;

    // get the post count value, defaulting to 0 if not available
    const postCount =
      locationInfo && locationInfo.postCount !== undefined
        ? locationInfo.postCount
        : postsData.length || 0;

    // display location details with posts
    mainContent.innerHTML = `
      <h2>${locationName}</h2>
      <div class="location-info">
        ${
          locationInfo
            ? `
          <p><strong>Country:</strong> ${
            locationInfo.countryName || "Not specified"
          }</p>
          <p><strong>Break Type:</strong> ${
            locationInfo.breakType || "Not specified"
          }</p>
          <p><strong>Surf Score:</strong> ${
            locationInfo.surfScore || "Not rated"
          }</p>
          <p><strong>Added by:</strong> ${locationInfo.userId || "Unknown"}</p>
          <p><strong>Post Count:</strong> <span id="location-post-count">${postCount}</span></p>
        `
            : ""
        }
      </div>
      <button id="back-to-locations" class="button">Back to Locations</button>
      ${createPostSection}
      <h3>Posts (${postsData.length}):</h3>
      <div id="post-tiles" class="tiles-container"></div>
    `;

    // add back button event listener
    document
      .getElementById("back-to-locations")
      .addEventListener("click", loadSurfLocations);

    // display all posts
    const postTiles = document.getElementById("post-tiles");

    // filter posts with valid _id and only use actual post data
    const posts = postsData.filter(
      (post) => post && (post._id || (post._id && post._id.$oid))
    );

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
          <p><strong>Comments:</strong> ${post.TotalComments || 0}</p>
        `;
        postTiles.appendChild(tile);
      });

      // attach event listeners for post tiles to load post details
      addPostTileEventListeners();
    }

    // create post event listener
    if (loggedInUser) {
      const createPostForm = document.getElementById("create-post-form");
      if (createPostForm) {
        createPostForm.addEventListener("submit", async (e) => {
          e.preventDefault();
          const description = document
            .getElementById("post-description")
            .value.trim();
          try {
            const response = await fetch(
              "http://localhost:3000/api/protected/create-post",
              {
                method: "POST",
                headers: getAuthHeaders(),
                body: JSON.stringify({
                  username: loggedInUser.username,
                  locationName: locationName,
                  description: description,
                }),
              }
            );
            const result = await response.json();
            if (result.success) {
              alert("Post created successfully!");

              const postCountElement = document.getElementById(
                "location-post-count"
              );
              if (postCountElement) {
                const currentCount =
                  parseInt(postCountElement.textContent) || 0;
                postCountElement.textContent = currentCount + 1;
              }
              loadLocationDetails(locationName);
            } else {
              alert(
                `Failed to create post: ${result.message || "Unknown error"}`
              );
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
      <button id="back-to-locations" class="button">Back to Locations</button>
      <p>Error loading location details. Please try again.</p>
    `;
    document
      .getElementById("back-to-locations")
      .addEventListener("click", loadSurfLocations);
  }
}

// function to like a comment
async function likeComment(commentId) {
  const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

  if (!loggedInUser) {
    alert("You must be logged in to like a comment.");
    return;
  }

  try {
    const response = await fetch(
      "http://localhost:3000/api/protected/like-comment",
      {
        method: "POST",
        headers: getAuthHeaders(),
        body: JSON.stringify({ userId: loggedInUser.userId, commentId }),
      }
    );

    const result = await response.json();

    if (result.success) {
      const likeCountElement = document.querySelector(
        `.like-count[data-comment-id="${commentId}"]`
      );
      if (likeCountElement) {
        likeCountElement.textContent =
          parseInt(likeCountElement.textContent) + 1;
      }
    } else {
      alert(`Failed to like comment: ${result.message || "Unknown error"}`);
    }
  } catch (error) {
    console.error("Error liking comment:", error);
    alert("An error occurred while liking the comment.");
  }
}

// function to create a comment
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
    const response = await fetch(
      "http://localhost:3000/api/protected/create-comment",
      {
        method: "POST",
        headers: getAuthHeaders(),
        body: JSON.stringify({
          postId,
          userId: loggedInUser.username,
          description,
        }),
      }
    );

    const result = await response.json();

    if (result.success) {
      alert("Comment created successfully!");
      loadPostDetails(postId);
    } else {
      alert(`Failed to create comment: ${result.message || "Unknown error"}`);
    }
  } catch (error) {
    console.error("Error creating comment:", error);
    alert("An error occurred while creating the comment.");
  }
}

// function to attach event listeners for post tiles
function addPostTileEventListeners() {
  const postTiles = document.querySelectorAll(".post-tile");
  postTiles.forEach((tile) => {
    const postId = tile.dataset.postId;
    if (!postId) {
      console.error("Post ID is undefined for a tile.");
      return;
    }
    tile.addEventListener("click", () => loadPostDetails(postId));
  });
}

// function to load details for a specific post
async function loadPostDetails(postId) {
  try {
    console.log("Loading post details for ID:", postId);
    const response = await fetch(
      `http://localhost:3000/api/post-comments?postId=${postId}`
    );
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

    document
      .getElementById("back-to-location")
      .addEventListener("click", () => {
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
          <p><strong>User:</strong> ${comment.userId}</p>
          <p><strong>Likes:</strong> <span class="like-count" data-comment-id="${
            comment.commentId
          }">${comment.TotalLikes || 0}</span></p>
          <button class="like-button" data-comment-id="${
            comment.commentId
          }">Like</button>
        `;
        commentTiles.appendChild(tile);
      });

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
          const description = document
            .getElementById("comment-description")
            .value.trim();
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

// updates UI if logged in
function updateAuthUI() {
  const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

  if (loggedInUser) {
    userDisplay.innerText = `Welcome, ${loggedInUser.username}`;
    signInButton.style.display = "none";
    signOutButton.style.display = "inline-block";

    const mainContent = document.getElementById("main-content");
    if (
      mainContent &&
      mainContent.querySelector(".search-bar") &&
      !document.getElementById("add-location-btn")
    ) {
      const searchBar = mainContent.querySelector(".search-bar");
      const addLocationBtn = document.createElement("button");
      addLocationBtn.id = "add-location-btn";
      addLocationBtn.className = "button";
      addLocationBtn.textContent = "Add New Location";
      addLocationBtn.addEventListener("click", showAddLocationForm);
      searchBar.appendChild(addLocationBtn);
    }
  } else {
    userDisplay.innerText = "";
    signInButton.style.display = "inline-block";
    signOutButton.style.display = "none";

    const addLocationBtn = document.getElementById("add-location-btn");
    if (addLocationBtn) {
      addLocationBtn.remove();
    }
  }
}

//signout event listener
signOutButton.addEventListener("click", async () => {
  try {
    const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

    if (loggedInUser && loggedInUser.username) {
      const response = await fetch("http://localhost:3000/api/logout", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${loggedInUser.token}`,
        },
        body: JSON.stringify({
          username: loggedInUser.username,
        }),
      });

      if (!response.ok) {
        console.error("Logout on server failed:", await response.text());
      } else {
        console.log("Server logout successful");
      }
    }
    localStorage.removeItem("loggedInUser");
    updateAuthUI();
    alert("Signed out successfully!");
    loadSurfLocations();
  } catch (error) {
    console.error("Error during sign out:", error);
    localStorage.removeItem("loggedInUser");
    updateAuthUI();
    alert("Signed out, but there may have been an issue with the server.");
    loadSurfLocations();
  }
});
