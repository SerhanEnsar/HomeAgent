from fastapi import FastAPI, Request, Form, Query, HTTPException
from fastapi.responses import HTMLResponse, RedirectResponse
from starlette.middleware.sessions import SessionMiddleware
from fastapi.templating import Jinja2Templates
from dotenv import load_dotenv
import os
import psutil
from fastapi.responses import HTMLResponse, RedirectResponse, JSONResponse
from pathlib import Path
import shutil
import mimetypes
from fastapi.responses import FileResponse
from pydantic import BaseModel

load_dotenv()

app = FastAPI()
templates = Jinja2Templates(directory="templates")

# Session middleware
app.add_middleware(SessionMiddleware, secret_key=os.getenv("SECRET_KEY", "fallback_secret"))

# Admin User
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
    return templates.TemplateResponse("login.html", {"request": request, "error": "İncorrect username or password."})

@app.get("/logout")
def logout(request: Request):
    request.session.clear()
    return RedirectResponse("/login", status_code=302)

@app.get("/")
def home(request: Request):
    if not is_logged_in(request):
        return RedirectResponse("/login", status_code=302)
    return templates.TemplateResponse("dashboard.html", {"request": request})

from fastapi import FastAPI, Request, Form, Query

@app.get("/api/status")
def api_status(api_key: str = Query(default="")):
    if api_key != os.getenv("API_KEY"):
        return JSONResponse({"error": "unauthorized"}, status_code=401)
    
    # CPU temperature
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

class RenameReq(BaseModel):
    mount: str
    path: str
    new_name: str

class DeleteReq(BaseModel):
    mount: str
    path: str

def _get_mounts():
    import subprocess
    out = subprocess.check_output(["df", "-h"], text=True).splitlines()
    mounts = []
    for line in out[1:]:
        parts = line.split()
        if len(parts) < 6:
            continue
        filesystem, size, used, avail, percent, mount = parts[:6]
        if filesystem == "tmpfs":
            continue
        if mount.startswith(("/proc", "/sys", "/dev", "/run", "/snap")):
            continue
        mounts.append({"filesystem": filesystem, "mount": mount, "size": size, "used": used, "avail": avail, "percent": percent})
    return mounts

def _find_mount(mount: str):
    for m in _get_mounts():
        if m["mount"] == mount:
            return m
    return None

def _resolve_in_mount(mount: str, path: str) -> Path:
    m = _find_mount(mount)
    if not m:
        raise HTTPException(status_code=400, detail="invalid_mount")
    base = Path(mount).resolve()
    rel = Path(path.lstrip("/"))
    target = (base / rel).resolve()
    if not str(target).startswith(str(base)):
        raise HTTPException(status_code=400, detail="invalid_path")
    return target

@app.get("/api/files/devices")
def api_files_devices(request: Request):
    if not is_logged_in(request):
        return JSONResponse({"error": "unauthorized"}, status_code=401)
    return _get_mounts()

@app.get("/api/files/list")
def api_files_list(request: Request, mount: str, path: str = ""):
    if not is_logged_in(request):
        return JSONResponse({"error": "unauthorized"}, status_code=401)
    m = _find_mount(mount)
    if not m:
        return {"error": "invalid_mount"}
    base = Path(mount).resolve()
    rel = Path(path.lstrip("/"))
    target = (base / rel).resolve()
    if not str(target).startswith(str(base)):
        return {"error": "invalid_path"}
    if not target.exists() or not target.is_dir():
        return {"error": "not_found"}
    items = []
    for p in sorted(target.iterdir(), key=lambda x: (not x.is_dir(), x.name.lower())):
        try:
            st = p.stat()
            items.append({"name": p.name, "type": "dir" if p.is_dir() else "file", "size": st.st_size if p.is_file() else None, "mtime": int(st.st_mtime)})
        except PermissionError:
            items.append({"name": p.name, "type": "unknown", "size": None, "mtime": None})
    return {"mount": mount, "path": str(rel), "items": items}

@app.get("/api/files/download")
def api_files_download(request: Request, mount: str, path: str):
    if not is_logged_in(request):
        return JSONResponse({"error": "unauthorized"}, status_code=401)
    target = _resolve_in_mount(mount, path)
    if not target.exists() or not target.is_file():
        raise HTTPException(status_code=404, detail="not_found")
    mime, _ = mimetypes.guess_type(str(target))
    return FileResponse(path=str(target), media_type=mime or "application/octet-stream", filename=target.name)

@app.post("/api/files/rename")
def api_files_rename(req: RenameReq, request: Request):
    if not is_logged_in(request):
        return JSONResponse({"error": "unauthorized"}, status_code=401)
    src = _resolve_in_mount(req.mount, req.path)
    if not src.exists():
        raise HTTPException(status_code=404, detail="not_found")
    if "/" in req.new_name or req.new_name.strip() == "":
        raise HTTPException(status_code=400, detail="bad_name")
    dst = src.parent / req.new_name
    if dst.exists():
        raise HTTPException(status_code=409, detail="already_exists")
    src.rename(dst)
    return {"ok": True}

@app.post("/api/files/delete")
def api_files_delete(req: DeleteReq, request: Request):
    if not is_logged_in(request):
        return JSONResponse({"error": "unauthorized"}, status_code=401)
    target = _resolve_in_mount(req.mount, req.path)
    if not target.exists():
        raise HTTPException(status_code=404, detail="not_found")
    if target.is_dir():
        shutil.rmtree(target)
    else:
        target.unlink()
    return {"ok": True}