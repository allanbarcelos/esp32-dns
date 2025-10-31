import { Link, useLocation } from "react-router-dom";

export default function Sidebar() {
  const { pathname } = useLocation();

  return (
    <aside>
      <nav>
        <ul>
          <li><Link to="/" className={pathname === "/" ? "active" : ""}>Dashboard</Link></li>
          <li><Link to="/cloudflare" className={pathname === "/cloudflare" ? "active" : ""}>Cloudflare</Link></li>
        </ul>
      </nav>
    </aside>
  );
}
