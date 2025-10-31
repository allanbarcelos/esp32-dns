import { Routes, Route } from "react-router-dom";
import Login from "./pages/Login";
import Register from "./pages/Register";
import Dashboard from "./pages/Dashboard";
import Cloudflare from "./pages/Cloudflare";
import MainLayout from "./layout/MainLayout";

export default function App() {
  return (
    <Routes>
      {/* Rotas independentes */}
      <Route path="/login" element={<Login />} />
      <Route path="/register" element={<Register />} />

      {/* Rotas com layout comum */}
      <Route element={<MainLayout />}>
        <Route path="/" element={<Dashboard />} />
        <Route path="/cloudflare" element={<Cloudflare />} />
      </Route>
    </Routes>
  );
}
