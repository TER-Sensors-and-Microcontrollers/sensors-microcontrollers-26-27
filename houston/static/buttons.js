/*
    buttons.js
    
    logic for popups + buttons
*/
        
        
document.addEventListener('DOMContentLoaded', function() {
  const openWeb = document.getElementById('openWeb');
  const uploadDB = document.getElementById('upload');
  const fileInput = document.getElementById('file-input');
  const scatterOn = document.getElementById('scatterOn');
  const scatterOff = document.getElementById('scatterOff');

  if (openWeb) {
    openWeb.onclick = () => {
      window.location.href = "/SD";
    };
  }

  if (uploadDB && fileInput) {
    uploadDB.addEventListener("click", () => {
      fileInput.click();
    });

    fileInput.addEventListener("change", async (e) => {
      const status = document.getElementById("upload-status");
      const file = e.target.files[0];
      if (!file) return;                       // picker was closed without choosing

      // quick check in the browser; the server checks again
      if (!file.name.endsWith(".db")) {
        status.textContent = "Not a DB file: " + file.name;
        return;
      }

      // package the file the same way curl -F "file=@..." does
      const formData = new FormData();
      formData.append("file", file);

      status.textContent = "Uploading " + file.name + "...";
      try {
        const response = await fetch("/upload", { method: "POST", body: formData });
        const result = await response.json();
        if (response.ok) {
          status.textContent = "Uploaded " + file.name + ": " + result.readings + " readings";
        } else {
          status.textContent = "Upload failed: " + result.error;
        }
      } catch (error) {
        status.textContent = "Upload failed: could not reach the server";
        console.error(error);
      }

      fileInput.value = "";                    // so picking the same file again still triggers "change"
    });
  }

  if (scatterOn && scatterOff) {
    scatterOn.onclick = () => {
      scatterOff.classList.remove("hidden");
      scatterOn.classList.add("hidden");
      scatterX.classList.remove("hidden")
      document.getElementById("sc")?.classList.remove("hidden");
      document.getElementById("scatterChoice")?.classList.remove("hidden");
    };

    scatterOff.onclick = () => {
      scatterOff.classList.add("hidden");
      scatterOn.classList.remove("hidden");
      document.getElementById("sc")?.classList.add("hidden");
      document.getElementById("scatterChoice")?.classList.add("hidden");
    };
  }
});
