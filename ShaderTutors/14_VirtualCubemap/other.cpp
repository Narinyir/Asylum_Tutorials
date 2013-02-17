//*************************************************************************************************************
#define TITLE			"Shader tutorial 14: Virtual cubemap variance shadow mapping"
#define MYERROR(x)		{ std::cout << "* Error: " << x << "!\n"; }
#define SAFE_RELEASE(x)	{ if( (x) ) { (x)->Release(); (x) = NULL; } }

#include <d3dx9.h>
#include <iostream>

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

HWND							hwnd			= NULL;
LPDIRECT3D9						direct3d		= NULL;
LPDIRECT3DDEVICE9				device			= NULL;
LPD3DXEFFECT					variance		= NULL;
LPD3DXEFFECT					distance		= NULL;
LPD3DXEFFECT					specular		= NULL;
LPD3DXEFFECT					blur			= NULL;
LPDIRECT3DVERTEXDECLARATION9	vertexdecl		= NULL;

LPD3DXMESH						shadowreceiver	= NULL;
LPD3DXMESH						shadowcaster	= NULL;
LPDIRECT3DTEXTURE9				texture1		= NULL;
LPDIRECT3DTEXTURE9				texture2		= NULL;
LPDIRECT3DTEXTURE9				shadowmap		= NULL;
LPDIRECT3DTEXTURE9				blurshadowmap	= NULL;
LPDIRECT3DCUBETEXTURE9			virtualcube		= NULL;
LPDIRECT3DSURFACE9				depthstencil	= NULL;
LPDIRECT3DSURFACE9				shadowsurf		= NULL;

D3DPRESENT_PARAMETERS			d3dpp;
RECT							workarea;
long							screenwidth		= 800;
long							screenheight	= 600;

HRESULT InitScene();

void Update(float delta);
void Render(float alpha, float elapsedtime);

void CreateCubeMatrices(D3DXMATRIX out[6], const D3DXVECTOR3& eyepos)
{
	D3DXVECTOR3 look, up;

	// left
	look = D3DXVECTOR3(-1, eyepos.y, 0);
	up = D3DXVECTOR3(0, 1, 0);
	
	D3DXMatrixLookAtLH(&out[0], &eyepos, &look, &up);

	out[0]._31 *= -1;
	out[0]._41 *= -1;

	// right
	look.x = 1;
	D3DXMatrixLookAtLH(&out[1], &eyepos, &look, &up);

	out[1]._31 *= -1;
	out[1]._41 *= -1;

	// front
	look.x = 0;
	look.z = -1;

	D3DXMatrixLookAtLH(&out[2], &eyepos, &look, &up);

	out[2]._11 *= -1;
	out[2]._41 *= -1;

	// back
	look.z = 1;

	D3DXMatrixLookAtLH(&out[3], &eyepos, &look, &up);

	out[3]._11 *= -1;
	out[3]._41 *= -1;

	// top
	look = D3DXVECTOR3(0, eyepos.y + 1, 0);
	up = D3DXVECTOR3(0, 0, 1);
	
	D3DXMatrixLookAtLH(&out[4], &eyepos, &look, &up);

	out[4]._11 *= -1;
	out[4]._41 *= -1;

	// bottom
	look = D3DXVECTOR3(0, eyepos.y - 1, 0);
	up.z = -1;

	D3DXMatrixLookAtLH(&out[5], &eyepos, &look, &up);
	
	out[5]._11 *= -1;
	out[5]._41 *= -1;
}
//*************************************************************************************************************
void DebugVisualize(LPDIRECT3DBASETEXTURE9 tex, float width, float height)
{
	float tmpvertices[36] =
	{
		-0.5f,			-0.5f,			0, 1, 0, 0,
		width - 0.5f,	-0.5f,			0, 1, 1, 0,
		-0.5f,			height - 0.5f,	0, 1, 0, 1,

		-0.5f,			height - 0.5f,	0, 1, 0, 1,
		width - 0.5f,	-0.5f,			0, 1, 1, 0,
		width - 0.5f,	height - 0.5f,	0, 1, 1, 1
	};

	device->SetVertexShader(NULL);
	device->SetPixelShader(NULL);

	device->SetTexture(0, tex);
	device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);

	device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, tmpvertices, sizeof(D3DXVECTOR4) + sizeof(D3DXVECTOR2));
	device->SetTexture(0, NULL);
}
//*************************************************************************************************************
void FillCubeFace(LPDIRECT3DCUBETEXTURE9 tex, D3DCUBEMAP_FACES face, UINT facelength, const D3DXVECTOR4& coords)
{
	D3DLOCKED_RECT rect;

	float xstep = (coords.y - coords.x) / (float)facelength;
	float ystep = (coords.w - coords.z) / (float)facelength;

	if( SUCCEEDED(tex->LockRect(face, 0, &rect, NULL, 0)) )
	{
		for( UINT i = 0; i < facelength; ++i )
		{
			for( UINT j = 0; j < facelength; ++j )
			{
				*((D3DXVECTOR2*)rect.pBits + i * facelength + j) =
					D3DXVECTOR2(coords.x + j * xstep, coords.z + i * ystep);
			}
		}

		tex->UnlockRect(face, 0);
	}
}
//*************************************************************************************************************
HRESULT DXCreateEffect(const char* file, LPD3DXEFFECT* out)
{
	HRESULT hr;
	LPD3DXBUFFER errors = NULL;

	if( FAILED(hr = D3DXCreateEffectFromFileA(device, file, NULL, NULL, D3DXSHADER_DEBUG, NULL, out, &errors)) )
	{
		if( errors )
		{
			char* str = (char*)errors->GetBufferPointer();
			std::cout << str << "\n\n";
		}
	}

	if( errors )
		errors->Release();

	return hr;
}
//*************************************************************************************************************
HRESULT InitDirect3D(HWND hwnd)
{
	if( NULL == (direct3d = Direct3DCreate9(D3D_SDK_VERSION)) )
		return E_FAIL;

	d3dpp.BackBufferFormat				= D3DFMT_X8R8G8B8;
	d3dpp.BackBufferCount				= 1;
	d3dpp.BackBufferHeight				= screenheight;
	d3dpp.BackBufferWidth				= screenwidth;
	d3dpp.AutoDepthStencilFormat		= D3DFMT_D24S8;
	d3dpp.hDeviceWindow					= hwnd;
	d3dpp.Windowed						= true;
	d3dpp.EnableAutoDepthStencil		= true;
	d3dpp.PresentationInterval			= D3DPRESENT_INTERVAL_IMMEDIATE;
	d3dpp.FullScreen_RefreshRateInHz	= D3DPRESENT_RATE_DEFAULT;
	d3dpp.SwapEffect					= D3DSWAPEFFECT_DISCARD;
	d3dpp.MultiSampleType				= D3DMULTISAMPLE_NONE;
	d3dpp.MultiSampleQuality			= 0;

	if( FAILED(direct3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
		D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &device)) )
	{
		MYERROR("Could not create Direct3D device");
		return E_FAIL;
	}

	device->SetRenderState(D3DRS_LIGHTING, false);

	device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);

	device->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);

	device->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(2, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);

	device->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

	return S_OK;
}
//*************************************************************************************************************
LRESULT WINAPI WndProc(HWND hWnd, unsigned int msg, WPARAM wParam, LPARAM lParam)
{
	switch( msg )
	{
	case WM_CLOSE:
		ShowWindow(hWnd, SW_HIDE);
		DestroyWindow(hWnd);
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_KEYUP:
		switch(wParam)
		{
		case VK_ESCAPE:
			SendMessage(hWnd, WM_CLOSE, 0, 0);
			break;
		}
		break;

	default:
		break;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}
//*************************************************************************************************************
void Adjust(tagRECT& out, long& width, long& height, DWORD style, DWORD exstyle, bool menu = false)
{
	long w = workarea.right - workarea.left;
	long h = workarea.bottom - workarea.top;

	out.left = (w - width) / 2;
	out.top = (h - height) / 2;
	out.right = (w + width) / 2;
	out.bottom = (h + height) / 2;

	AdjustWindowRectEx(&out, style, false, 0);

	long windowwidth = out.right - out.left;
	long windowheight = out.bottom - out.top;

	long dw = windowwidth - width;
	long dh = windowheight - height;

	if( windowheight > h )
	{
		float ratio = (float)width / (float)height;
		float realw = (float)(h - dh) * ratio + 0.5f;

		windowheight = h;
		windowwidth = (long)floor(realw) + dw;
	}

	if( windowwidth > w )
	{
		float ratio = (float)height / (float)width;
		float realh = (float)(w - dw) * ratio + 0.5f;

		windowwidth = w;
		windowheight = (long)floor(realh) + dh;
	}

	out.left = workarea.left + (w - windowwidth) / 2;
	out.top = workarea.top + (h - windowheight) / 2;
	out.right = workarea.left + (w + windowwidth) / 2;
	out.bottom = workarea.top + (h + windowheight) / 2;

	width = windowwidth - dw;
	height = windowheight - dh;
}
//*************************************************************************************************************
int main(int argc, char* argv[])
{
	LARGE_INTEGER qwTicksPerSec = { 0, 0 };
	LARGE_INTEGER qwTime;
	LONGLONG tickspersec;
	double last, current;
	double delta, accum = 0;

	// ablak oszt�ly
	WNDCLASSEX wc =
	{
		sizeof(WNDCLASSEX),
		CS_CLASSDC,
		(WNDPROC)WndProc,
		0L,
		0L,
		GetModuleHandle(NULL),
		NULL, NULL, NULL, NULL, "TestClass", NULL
	};

	RegisterClassEx(&wc);
	SystemParametersInfo(SPI_GETWORKAREA, 0, &workarea, 0);

	RECT rect = { 0, 0, screenwidth, screenheight };
	DWORD style = WS_CLIPCHILDREN|WS_CLIPSIBLINGS;

	// ablakos m�d
	style |= WS_SYSMENU|WS_BORDER|WS_CAPTION;
	Adjust(rect, screenwidth, screenheight, style, 0);

	hwnd = CreateWindowA("TestClass", TITLE, style,
			rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
			NULL, NULL, wc.hInstance, NULL);

	if( !hwnd )
	{
		MYERROR("Could not create window");
		goto _end;
	}

	if( FAILED(InitDirect3D(hwnd)) )
	{
		MYERROR("Failed to initialize Direct3D");
		goto _end;
	}
	
	if( FAILED(InitScene()) )
	{
		MYERROR("Failed to initialize scene");
		goto _end;
	}

	ShowWindow(hwnd, SW_SHOWDEFAULT);
	UpdateWindow(hwnd);

	MSG msg;
	ZeroMemory(&msg, sizeof(msg));

	POINT p;
	GetCursorPos(&p);
	ScreenToClient(hwnd, &p);

	// timer
	QueryPerformanceFrequency(&qwTicksPerSec);
	tickspersec = qwTicksPerSec.QuadPart;

	QueryPerformanceCounter(&qwTime);
	last = (double)qwTime.QuadPart / (double)tickspersec;

	while( msg.message != WM_QUIT )
	{
		QueryPerformanceCounter(&qwTime);

		current = (double)qwTime.QuadPart / (double)tickspersec;
		delta = (current - last);

		last = current;
		accum += delta;

		while( accum > 0.0333f )
		{
			accum -= 0.0333f;

			while( PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE) )
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			Update(0.0333f);
		}

		if( msg.message != WM_QUIT )
			Render((float)accum / 0.0333f, (float)delta);
	}

	_end:
	SAFE_RELEASE(vertexdecl);
	SAFE_RELEASE(depthstencil);
	SAFE_RELEASE(shadowsurf);
	SAFE_RELEASE(shadowreceiver);
	SAFE_RELEASE(shadowcaster);
	SAFE_RELEASE(distance);
	SAFE_RELEASE(variance);
	SAFE_RELEASE(blur);
	SAFE_RELEASE(specular);
	SAFE_RELEASE(texture1);
	SAFE_RELEASE(texture2);
	SAFE_RELEASE(shadowmap);
	SAFE_RELEASE(virtualcube);
	SAFE_RELEASE(blurshadowmap);

	if( device )
	{
		ULONG rc = device->Release();

		if( rc > 0 )
			MYERROR("You forgot to release something");
	}

	if( direct3d )
		direct3d->Release();

	UnregisterClass("TestClass", wc.hInstance);
	_CrtDumpMemoryLeaks();

#ifdef _DEBUG
	system("pause");
#endif

	return 0;
}
//*************************************************************************************************************