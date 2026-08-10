#pragma once

#include "UEPySListView.h"

extern PyTypeObject ue_PySPythonListViewType;

class SPythonListView : public SListView<TSharedPtr<struct FPythonItem>>
{
public:
	~SPythonListView()
	{
		ClearItemsSource();
		delete PythonItemsSource;
	}

	void SetPythonItemsSource(TArray<TSharedPtr<FPythonItem>> *InItemsSource)
	{
		PythonItemsSource = InItemsSource;
	}

	TArray<TSharedPtr<FPythonItem>>& GetMutablePythonItemsSource()
	{
		check(PythonItemsSource);
		return *PythonItemsSource;
	}

	void SetHeaderRow(TSharedPtr<SHeaderRow> InHeaderRowWidget);

private:
	TArray<TSharedPtr<FPythonItem>> *PythonItemsSource = nullptr;
};

typedef struct
{
	ue_PySListView s_list_view;
} ue_PySPythonListView;

void ue_python_init_spython_list_view(PyObject *);
