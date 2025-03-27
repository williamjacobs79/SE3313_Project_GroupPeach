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

// Show Modal
signInButton.addEventListener("click", () => {
  modal.style.display = "flex";
});

// Close Modal - test
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

//note: we understand that hard-coding localhost:3000 is not good practice in industry

//login button event listener
loginButton.addEventListener("click", async () => {
  const username = document.getElementById("username").value;
  const password = document.getElementById("password").value;

  try {
    const response = await fetch("http://localhost:3000/api/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username, password }),
    });

    const result = await response.json();

    if (result.success) {
      userDisplay.innerText = `Welcome, ${username}`;
      modal.style.display = "none";

      // Store logged-in user information, including userId in browser storage
      localStorage.setItem(
        "loggedInUser",
        JSON.stringify({ userId: result.userId, username })
      );
    } else {
      alert(result.message || "Login failed. Please try again.");
    }
  } catch (error) {
    console.error("Error during login:", error);
    alert("An error occurred during login.");
  }
});

//create new user event listener
createButton.addEventListener("click", async () => {
  const newUsername = document.getElementById("new-username").value;
  const newPassword = document.getElementById("new-password").value;
  const newEmail = document.getElementById("new-email").value;

  try {
    const response = await fetch("http://localhost:3000/api/create-account", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
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
      alert(`Failed to create account: ${result.error || "Unknown error"}`);
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

//responsible for the surf-location 'page'
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

//looads the surf location overviews
async function fetchAndDisplayLocations(
  country = "",
  location = "",
  filterLikes = false
) {
  try {
    const response = await fetch(
      `http://localhost:3000/api/surf-locations?country=${country}&location=${location}&filterLikes=${filterLikes}`
    );
    const locations = await response.json();

    const tilesContainer = document.getElementById("surf-locations");
    tilesContainer.innerHTML = ""; // Clear existing tiles

    if (locations.length === 0) {
      tilesContainer.innerHTML = `<p>No surf locations found.</p>`;
      return;
    }

    // Create tiles for each location
    locations.forEach((loc) => {
      const tile = document.createElement("div");
      tile.classList.add("tile");
      tile.innerHTML = `
                <h3>${loc.locationName}</h3>
                <p>Break Type: ${loc.breakType}</p>
                <p>Surf Score: ${loc.surfScore}</p>
                <p>Country: ${loc.countryName}</p>
                <p>Added by User ID: ${loc.userId}</p>
                <p>Likes: ${loc.TotalLikes || 0}</p>
                <p>Comments: ${loc.TotalComments || 0}</p>
            `;
      tile.addEventListener("click", () =>
        loadLocationDetails(loc.locationName)
      );
      tilesContainer.appendChild(tile);
    });
  } catch (error) {
    console.error("Error fetching surf locations:", error);
  }
}

//gets the location details when a location is clicked on
async function loadLocationDetails(locationName) {
  try {
    const response = await fetch(
      `http://localhost:3000/api/location-details?locationName=${locationName}`
    );
    const data = await response.json();

    const mainContent = document.getElementById("main-content");
    mainContent.innerHTML = ""; // Clear previous content

    if (data.length === 0) {
      mainContent.innerHTML = `<p>No data available for this location.</p>`;
      return;
    }

    // get location information
    const location = data[0];
    const posts = data.filter((post) => post.postId !== null);

    // display the location details
    mainContent.innerHTML = `
            <h2>${locationName}</h2>
            <div id="location-info">
                <h3>Risks:</h3>
                <div id="risks-section"></div>
                <h3>Weather Conditions:</h3>
                <div id="weather-section">
                    <form id="weather-form">
                        <input type="date" id="weather-date" required />
                        <button type="submit">Get Weather</button>
                    </form>
                    <div id="weather-results"></div>
                </div>
            </div>
            <h3>Posts:</h3>
            <div id="post-tiles" class="tiles-container"></div>
        `;

    //load all risks for location
    await loadSurfRisks(locationName);

    // display all posts
    const postTiles = document.getElementById("post-tiles");
    posts.forEach((post) => {
      const tile = document.createElement("div");
      tile.classList.add("tile", "post-tile");
      tile.dataset.postId = post.postId;
      tile.innerHTML = `
                <h3>Post ID: ${post.postId}</h3>
                <p>${post.descript}</p>
                <p>Likes: ${post.TotalLikes || 0}</p>
                <p>Comments: ${post.TotalComments || 0}</p>
            `;
      postTiles.appendChild(tile);
    });

    // weather form
    const weatherForm = document.getElementById("weather-form");
    weatherForm.addEventListener("submit", async (e) => {
      e.preventDefault();
      const date = document.getElementById("weather-date").value;
      await loadWeatherConditions(locationName, date);
    });

    // top post button and event
    const topPostsButton = document.createElement("button");
    topPostsButton.innerText = "Show Top Posts";
    topPostsButton.classList.add("top-posts-btn");
    topPostsButton.addEventListener("click", () =>
      fetchAndDisplayTopPosts(locationName)
    );
    mainContent.appendChild(topPostsButton);

    // attach event listeners for post tiles
    addPostTileEventListeners();
  } catch (error) {
    console.error("Error loading location details:", error);
  }
}

//loads in surf location risks for passe din locaiton
async function loadSurfRisks(locationName) {
  try {
    const response = await fetch(`http://localhost:3000/api/surf-risks`);
    const risks = await response.json();

    const risksSection = document.getElementById("risks-section");
    const locationRisks = risks.find(
      (risk) => risk.locationName === locationName
    );

    if (!locationRisks || !locationRisks.Risks) {
      risksSection.innerHTML = `<p>No risks associated with this location.</p>`;
    } else {
      risksSection.innerHTML = `<p>${locationRisks.Risks}</p>`;
    }
  } catch (error) {
    console.error("Error loading surf risks:", error);
  }
}

//loads weather information (for a user-inputted date, and selected location)
async function loadWeatherConditions(locationName, date) {
  try {
    const response = await fetch(
      `http://localhost:3000/api/weather-conditions`,
      {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ locationName, date }),
      }
    );
    const weather = await response.json();

    const weatherResults = document.getElementById("weather-results");
    if (weather.length === 0) {
      weatherResults.innerHTML = `<p>No weather data available for the selected date.</p>`;
    } else {
      const weatherDetails = weather[0];
      weatherResults.innerHTML = `
                <p><strong>Date:</strong> ${weatherDetails.wTimeStamp}</p>
                <p><strong>Wave Size:</strong> ${weatherDetails.waveSize} m</p>
                <p><strong>Wind Speed:</strong> ${
                  weatherDetails.windSpeed
                } km/h</p>
                <p><strong>Precipitation:</strong> ${
                  weatherDetails.precipitation ? "Yes" : "No"
                }</p>
            `;
    }
  } catch (error) {
    console.error("Error loading weather conditions:", error);
  }
}

//displays the top 5 posts (when top posts is clicked)
async function fetchAndDisplayTopPosts(locationName) {
  try {
    const response = await fetch(
      `http://localhost:3000/api/location-top-posts?locationName=${locationName}`
    );
    const topPosts = await response.json();

    const postTiles = document.getElementById("post-tiles");
    postTiles.innerHTML = ""; // Clear existing posts

    if (topPosts.length === 0) {
      postTiles.innerHTML = `<p>No top posts found for this location.</p>`;
      return;
    }

    topPosts.forEach((post) => {
      const tile = document.createElement("div");
      tile.classList.add("tile");
      tile.innerHTML = `
                <h3>Post ID: ${post.postId}</h3>
                <p>${post.descript}</p>
                <p>Likes: ${post.TotalLikes || 0}</p>
                <p>Comments: ${post.TotalComments || 0}</p>
                <p>Total Interactions: ${post.TotalInteractions || 0}</p>
            `;
      postTiles.appendChild(tile);
    });
  } catch (error) {
    console.error("Error fetching top posts:", error);
  }
}

//loads comments and liks on comments for a post (when post is clicked on)
async function loadPostDetails(postId) {
  try {
    const response = await fetch(
      `http://localhost:3000/api/post-comments?postId=${postId}`
    );
    const comments = await response.json();

    const mainContent = document.getElementById("main-content");
    const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

    mainContent.innerHTML = `
            <h2>Post Details</h2>
            <div id="post-details"></div>
            <h3>Comments:</h3>
            <div id="comment-tiles" class="tiles-container"></div>
            ${
              loggedInUser
                ? `
                <h3>Add a Comment:</h3>
                <form id="create-comment-form">
                    <textarea id="comment-description" placeholder="Write your comment here..." required></textarea>
                    <button type="submit">Upload Comment</button>
                </form>
            `
                : `<p>You must be logged in to add a comment.</p>`
            }
        `;

    // display comments
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
                    <p><strong>Likes:</strong> <span class="like-count" data-comment-id="${
                      comment.commentId
                    }">${comment.TotalLikes || 0}</span></p>
                    <button class="like-button" data-comment-id="${
                      comment.commentId
                    }">Like</button>
                `;
        commentTiles.appendChild(tile);
      });
    }

    // event listeners for like buttons
    const likeButtons = document.querySelectorAll(".like-button");
    likeButtons.forEach((button) => {
      button.addEventListener("click", async () => {
        const commentId = button.dataset.commentId;
        await likeComment(commentId);
      });
    });

    //new comment submission
    if (loggedInUser) {
      const createCommentForm = document.getElementById("create-comment-form");
      createCommentForm.addEventListener("submit", async (e) => {
        e.preventDefault();
        const description = document
          .getElementById("comment-description")
          .value.trim();
        await createComment(postId, description);
      });
    }
  } catch (error) {
    console.error("Error loading post details:", error);
  }
}

//when a user likes a comment, passes in commentId for the comment that was liked
async function likeComment(commentId) {
  const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

  if (!loggedInUser) {
    alert("You must be logged in to like a comment.");
    return;
  }

  try {
    const response = await fetch("http://localhost:3000/api/like-comment", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ userId: loggedInUser.userId, commentId }),
    });

    const result = await response.json();

    if (result.success) {
      // Update the like count dynamically
      const likeCountElement = document.querySelector(
        `.like-count[data-comment-id="${commentId}"]`
      );
      likeCountElement.textContent = parseInt(likeCountElement.textContent) + 1;
    } else {
      alert(`Failed to like comment: ${result.error || "Unknown error"}`);
    }
  } catch (error) {
    console.error("Error liking comment:", error);
    alert("An error occurred while liking the comment.");
  }
}

//when a user creates a comment
async function createComment(postId, description) {
  const loggedInUser = JSON.parse(localStorage.getItem("loggedInUser"));

  if (!loggedInUser) {
    alert("You must be logged in to create a comment.");
    console.error("Error: No logged-in user found.");
    return;
  }

  console.log("Post ID:", postId); // debuggin log
  console.log("User ID:", loggedInUser.userId); // debuggin log
  console.log("Description:", description); // debuggin log

  try {
    const response = await fetch("http://localhost:3000/api/create-comment", {
      method: "POST",
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
      alert(`Failed to create comment: ${result.error || "Unknown error"}`);
    }
  } catch (error) {
    console.error("Error creating comment:", error);
    alert("An error occurred while creating the comment.");
  }
}

//event listeners for the post tiles
function addPostTileEventListeners() {
  const postTiles = document.querySelectorAll(".post-tile");
  postTiles.forEach((tile) => {
    const postId = tile.dataset.postId; // Ensure this captures the correct postId
    if (!postId) {
      console.error("Post ID is undefined for a tile.");
      return;
    }
    tile.addEventListener("click", () => loadPostDetails(postId));
  });
}