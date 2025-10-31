import { Outlet } from "react-router-dom";
import Sidebar from "../components/Sidebar";

export default function MainLayout() {
  return (
    <div className="layout">
      <Sidebar />
      <main>
        <Outlet />
      </main>
    </div>
  );
}
