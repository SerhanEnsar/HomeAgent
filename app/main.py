from fastapi import FastAPI, Request, Form, Query
from fastapi.responses import HTMLResponse, RedirectResponse, JSONResponse
from starlette.middleware.sessions import SessionMiddleware
from fastapi.templating import Jinja2Templates
from dotenv import load_dotenv
from app.files import router as files_router
import os
import psutil
import httpx

load_dotenv()

app = FastAPI()
app.include_router(files_router)
templates = Jinja2Templates(directory="templates")

app.add_middleware(SessionMiddleware, secret_key=os.getenv("SECRET_KEY", "fallback_secret"))

USERNAME = os.getenv("USERNAME", "admin")
PASSWORD = os.getenv("PASSWORD", "changeme")

def is_logged_in(request: Request) -> bool:
    return bool(request.session.get("user"))

@app.get("/login", response_class=HTMLResponse)
def login_page(request: Request):
    if is_logged_in(request):
        return RedirectResponse("/", status_code=302)
    return templates.TemplateResponse("login.html", {"request": request, "error": ""})

@app.post("/login")
def login_submit(request: Request, username: str = Form(...), password: str = Form(...)):
    if username == USERNAME and password == PASSWORD:
        request.session["user"] = username
        return RedirectResponse("/", status_code=302)
    return templates.TemplateResponse("login.html", {"request": request, "error": "Incorrect username or password."})

@app.get("/logout")
def logout(request: Request):
    request.session.clear()
    return RedirectResponse("/login", status_code=302)

@app.get("/", response_class=HTMLResponse)
def home(request: Request):
    if not is_logged_in(request):
        return RedirectResponse("/login", status_code=302)
    return templates.TemplateResponse("dashboard.html", {"request": request, "active_page": "dashboard"})

@app.get("/api/status")
def api_status(api_key: str = Query(default="")):
    if api_key != os.getenv("API_KEY"):
        return JSONResponse({"error": "unauthorized"}, status_code=401)
    try:
        temp_str = open("/sys/class/thermal/thermal_zone0/temp").read().strip()
        cpu_temp = round(int(temp_str) / 1000, 1)
    except:
        cpu_temp = None
    return {
        "cpu_percent": psutil.cpu_percent(interval=0.2),
        "ram_percent": psutil.virtual_memory().percent,
        "disk_percent": psutil.disk_usage("/").percent,
        "cpu_temp": cpu_temp,
    }

@app.get("/files", response_class=HTMLResponse)
def files_page(request: Request):
    if not is_logged_in(request):
        return RedirectResponse("/login", status_code=302)
    return templates.TemplateResponse("files.html", {"request": request, "active_page": "files"})

@app.get("/trash", response_class=HTMLResponse)
def trash_page(request: Request):
    if not is_logged_in(request):
        return RedirectResponse("/login", status_code=302)
    return templates.TemplateResponse("trash.html", {"request": request, "active_page": "trash"})

@app.get("/api/info")
def api_info():
    import socket
    import subprocess
    ip = subprocess.check_output(["hostname", "-I"], text=True).strip().split()[0]
    hostname = socket.gethostname()
    ssid = subprocess.check_output(["nmcli", "-t", "-f", "active,ssid", "dev", "wifi"], text=True)
    wifi = "—"
    for line in ssid.splitlines():
        if line.startswith("yes:"):
            wifi = line.split("yes:")[1]
            break
    return {
        "ip": ip,
        "hostname": hostname,
        "wifi": wifi,
        "username": "serhanensar",
        "version": "1.0.0"
    }

@app.post("/api/chat")
async def api_chat(request: Request, api_key: str = Query(default="")):
    if api_key != os.getenv("API_KEY"):
        return JSONResponse({"error": "unauthorized"}, status_code=401)
    
    data = await request.json()
    message = data.get("message", "")
    
    async with httpx.AsyncClient() as client:
        resp = await client.post(
            "http://localhost:11434/api/generate",
            json={
                "model": "mistral:7b",
                "prompt": message,
                "system": "Sen HomeAgent adlı bir Pi asistanısın. Kısa ve öz Türkçe cevaplar ver. Sistem bilgisi sorulursa yardımcı ol.",
                "stream": False
            },
            timeout=60
        )
    
    result = resp.json()
    return {"response": result.get("response", "")}