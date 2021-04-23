
#include <oleidl.h>

class droper : public IDropTarget
{
public:
  ULONG AddRef() { return 1; }
  ULONG Release() { return 0; }

  HRESULT QueryInterface(REFIID riid, void **ppvObject)
  {
    if (riid == IID_IDropTarget)
    {
      *ppvObject = this;
      return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
  };

  HRESULT DragEnter(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
  {
    *pdwEffect &= DROPEFFECT_COPY;
    return S_OK;
  }

  HRESULT DragLeave() { return S_OK; }

  HRESULT DragOver(DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
  {
    *pdwEffect &= DROPEFFECT_COPY;
    return S_OK;
  }

  HRESULT Drop(IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect)
  {
    FORMATETC fmte = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM stgm;

    if (SUCCEEDED(pDataObj->GetData(&fmte, &stgm)))
    {
      HDROP hdrop = (HDROP)stgm.hGlobal;
      UINT file_count = DragQueryFile(hdrop, 0xFFFFFFFF, NULL, 0);

      for (UINT i = 0; i < file_count; i++)
      {
        TCHAR szFile[MAX_PATH];
        UINT cch = DragQueryFile(hdrop, i, szFile, MAX_PATH);
        if (cch > 0 && cch < MAX_PATH)
        {
        }
      }

      ReleaseStgMedium(&stgm);

    }

    *pdwEffect &= DROPEFFECT_COPY;
    return S_OK;
  }
};
