// Lists the notes, and adds one from the form.
async function load() {
  const notes = await (await fetch("/api/notes")).json();
  const list = document.getElementById("notes");
  list.textContent = "";
  for (const note of notes) {
    const item = document.createElement("li");
    item.textContent = note.text;
    list.appendChild(item);
  }
}

document.getElementById("add").addEventListener("submit", async (event) => {
  event.preventDefault();
  const input = document.getElementById("text");
  if (input.value.trim() === "") {
    return;
  }
  await fetch("/api/notes", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ text: input.value }) });
  input.value = "";
  await load();
});

load();
