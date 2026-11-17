"""Console-less launcher for development installs: pythonw launch.pyw"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from f1win.app import main  # noqa: E402

sys.exit(main())
