from fastapi import FastAPI, Request, Form
from fastapi.responses import HTMLResponse, RedirectResponse
from starlette.middleware.sessions import SessionMiddleware
import secrets

app = FastAPI()

# Session middleware
app.add_middleware(SessionMiddleware, secret_key=secrets.token_hex(32))

# Admin User
USERNAME = "serhanensar"
PASSWORD = "changeme"

def is_logged_in(request: Request) -> bool:
    return bool(request.session.get("user"))

@app.get("/login", response_class=HTMLResponse)
def login_page(request: Request):
    if is_logged_in(request):
        return RedirectResponse("/", status_code=302)
    return """
    <form method="post" action="/login">
        <input name="username" placeholder="Username">
        <input type="password" name="password" placeholder="Password">
        <button type="submit">Login</button>
    </form>
    """

@app.post("/login")
def login_submit(request: Request, username: str = Form(...), password: str = Form(...)):
    if username == USERNAME and password == PASSWORD:
        request.session["user"] = username
        return RedirectResponse("/", status_code=302)
    return RedirectResponse("/login", status_code=302)

@app.get("/logout")
def logout(request: Request):
    request.session.clear()
    return RedirectResponse("/login", status_code=302)

@app.get("/")
def home(request: Request):
    if not is_logged_in(request):
        return RedirectResponse("/login", status_code=302)
    return {"message": "Hoşgeldin " + request.session["user"]}
