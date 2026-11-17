---
status: accepted
---

# iPad control is a web page the app serves

The looper is controlled from one iPad on the performer's own router, with the Mac keyboard as the fallback when the link is slow. The app serves the page itself over HTTP and WebSocket. Safari's "Add to Home Screen" is the install. A native iPad app was rejected because it adds a second codebase and a signing setup that can expire days before a gig, for less than one frame of latency. OSC alone was rejected because it cannot show the folder list or the thumbnails, and a held gate has no disconnect signal. The protocol does not depend on the client, so a native app can still be added later without changing the server.
